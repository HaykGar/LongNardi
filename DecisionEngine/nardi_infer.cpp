#include "nardi_infer.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include "../CoreEngine/Auxilaries.h"

namespace nardi_py
{

namespace
{

// Feature layout, identical to binding_utils::fill_feature_matrix (the conv /
// legacy pipelines in nardi_net.py): a [6, 25] row-major float matrix.
//   rows 0-2 : current-player occupancy planes (24 board cells each)
//   rows 3-5 : opponent occupancy planes
//   last col : six per-row scalars
// Everything is divided by FEAT_SCALE.
constexpr int FEAT_ROWS = 6;
constexpr int BOARD_COLS = Nardi::ROWS * Nardi::COLS; // 24
constexpr int FEAT_COLS = BOARD_COLS + 1;             // 25
constexpr float FEAT_SCALE = 15.0f;

// model-kind tags written by nardi_net.export_weights
enum class ModelKind : uint32_t
{
    LEGACY = 0, // NardiNet (MLP)
    CONV = 1,   // ConvNardiNet (1 or 2 conv layers)
    RES = 2     // ResNardiNet
};

struct Tensor
{
    std::vector<int> shape;
    std::vector<float> data;

    int dim(int i) const { return shape.at(static_cast<size_t>(i)); }
};

using Weights = std::unordered_map<std::string, Tensor>;

struct Blob
{
    ModelKind kind;
    Weights weights;

    const Tensor& at(const std::string& name) const
    {
        auto it = weights.find(name);
        if(it == weights.end())
            throw std::runtime_error("nardi_infer: missing weight tensor '" + name + "'");
        return it->second;
    }

    bool has(const std::string& name) const { return weights.count(name) != 0; }
};

uint32_t read_u32(std::istream& in)
{
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if(!in)
        throw std::runtime_error("nardi_infer: unexpected end of weight file");
    // little-endian
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}

Blob read_blob(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if(!in)
        throw std::runtime_error("nardi_infer: cannot open weight file '" + path + "'");

    char magic[4];
    in.read(magic, 4);
    if(!in || magic[0] != 'N' || magic[1] != 'R' || magic[2] != 'D' || magic[3] != 'W')
        throw std::runtime_error("nardi_infer: bad magic in '" + path + "' (expected NRDW)");

    const uint32_t version = read_u32(in);
    if(version != 1u)
        throw std::runtime_error("nardi_infer: unsupported weight blob version");

    Blob blob;
    blob.kind = static_cast<ModelKind>(read_u32(in));
    const uint32_t n_tensors = read_u32(in);

    for(uint32_t t = 0; t < n_tensors; ++t)
    {
        const uint32_t name_len = read_u32(in);
        std::string name(name_len, '\0');
        in.read(name.data(), static_cast<std::streamsize>(name_len));

        const uint32_t ndim = read_u32(in);
        Tensor tensor;
        tensor.shape.resize(ndim);
        size_t count = 1;
        for(uint32_t d = 0; d < ndim; ++d)
        {
            const uint32_t dim = read_u32(in);
            tensor.shape[d] = static_cast<int>(dim);
            count *= dim;
        }

        tensor.data.resize(count);
        in.read(reinterpret_cast<char*>(tensor.data.data()),
                static_cast<std::streamsize>(count * sizeof(float)));
        if(!in)
            throw std::runtime_error("nardi_infer: truncated tensor '" + name + "'");

        blob.weights.emplace(std::move(name), std::move(tensor));
    }

    return blob;
}

// ---- feature extraction ------------------------------------------------

void fill_features(const Nardi::Board::Features& f, float* out, ModelKind kind)
{
    for(int row = 0; row < 3; ++row)
        for(int col = 0; col < BOARD_COLS; ++col)
            out[row * FEAT_COLS + col] = static_cast<float>(f.player.occ[row][col]) / FEAT_SCALE;

    for(int row = 0; row < 3; ++row)
        for(int col = 0; col < BOARD_COLS; ++col)
            out[(row + 3) * FEAT_COLS + col] = static_cast<float>(f.opp.occ[row][col]) / FEAT_SCALE;

    // Legacy uses square-occupancy; conv/res use pip-count (see nardi_net.py).
    const float third_player = (kind == ModelKind::LEGACY)
        ? static_cast<float>(f.player.sq_occ)
        : static_cast<float>(f.player.pip_count);
    const float third_opp = (kind == ModelKind::LEGACY)
        ? static_cast<float>(f.opp.sq_occ)
        : static_cast<float>(f.opp.pip_count);

    const float scalars[FEAT_ROWS] = {
        static_cast<float>(f.player.pieces_off),
        static_cast<float>(f.opp.pieces_off),
        third_player,
        third_opp,
        static_cast<float>(f.player.pieces_not_reached),
        static_cast<float>(f.opp.pieces_not_reached)
    };
    for(int row = 0; row < FEAT_ROWS; ++row)
        out[row * FEAT_COLS + (FEAT_COLS - 1)] = scalars[row] / FEAT_SCALE;
}

// ---- layer primitives --------------------------------------------------

// 1D convolution. in is [Cin, L] row-major; weight is [Cout, Cin, K]; bias [Cout].
// Returns [Cout, Lout] row-major with Lout = (L + 2*pad - K)/stride + 1.
std::vector<float> conv1d(const float* in, int Cin, int L,
                          const Tensor& weight, const Tensor& bias,
                          int stride, int pad)
{
    const int Cout = weight.dim(0);
    const int K = weight.dim(2);
    const int Lout = (L + 2 * pad - K) / stride + 1;

    std::vector<float> out(static_cast<size_t>(Cout) * Lout);
    for(int oc = 0; oc < Cout; ++oc)
    {
        const float b = bias.data[static_cast<size_t>(oc)];
        for(int ol = 0; ol < Lout; ++ol)
        {
            float acc = b;
            const int start = ol * stride - pad;
            for(int ic = 0; ic < Cin; ++ic)
            {
                const float* in_row = in + static_cast<size_t>(ic) * L;
                const float* w_row = weight.data.data() +
                                     ((static_cast<size_t>(oc) * Cin + ic) * K);
                for(int k = 0; k < K; ++k)
                {
                    const int ipos = start + k;
                    if(ipos < 0 || ipos >= L)
                        continue;
                    acc += w_row[k] * in_row[ipos];
                }
            }
            out[static_cast<size_t>(oc) * Lout + ol] = acc;
        }
    }
    return out;
}

// Fully connected: weight [out, in], bias [out].
std::vector<float> linear(const std::vector<float>& in, const Tensor& weight, const Tensor& bias)
{
    const int out_dim = weight.dim(0);
    const int in_dim = weight.dim(1);
    std::vector<float> out(static_cast<size_t>(out_dim));
    for(int o = 0; o < out_dim; ++o)
    {
        float acc = bias.data[static_cast<size_t>(o)];
        const float* w_row = weight.data.data() + static_cast<size_t>(o) * in_dim;
        for(int i = 0; i < in_dim; ++i)
            acc += w_row[i] * in[static_cast<size_t>(i)];
        out[static_cast<size_t>(o)] = acc;
    }
    return out;
}

// LayerNorm over the whole vector (normalized_shape == x.size()), affine.
void layer_norm_inplace(std::vector<float>& x, const Tensor& gamma, const Tensor& beta,
                        float eps = 1e-5f)
{
    const size_t n = x.size();
    double mean = 0.0;
    for(float v : x)
        mean += v;
    mean /= static_cast<double>(n);

    double var = 0.0;
    for(float v : x)
    {
        const double d = v - mean;
        var += d * d;
    }
    var /= static_cast<double>(n);

    const float inv = 1.0f / std::sqrt(static_cast<float>(var) + eps);
    for(size_t i = 0; i < n; ++i)
        x[i] = (x[i] - static_cast<float>(mean)) * inv * gamma.data[i] + beta.data[i];
}

void silu_inplace(std::vector<float>& x)
{
    for(float& v : x)
        v = v / (1.0f + std::exp(-v));
}

void relu_inplace(std::vector<float>& x)
{
    for(float& v : x)
        if(v < 0.0f)
            v = 0.0f;
}

// trunk + value head shared by every architecture: Linear/SiLU/Linear/SiLU/Linear,
// then softmax over 4 logits weighted by the `scores` buffer (out_dim == 4).
float trunk_value(std::vector<float> x, const Blob& w)
{
    auto h = linear(x, w.at("trunk.0.weight"), w.at("trunk.0.bias"));
    silu_inplace(h);
    h = linear(h, w.at("trunk.3.weight"), w.at("trunk.3.bias"));
    silu_inplace(h);
    auto logits = linear(h, w.at("trunk.6.weight"), w.at("trunk.6.bias"));

    const Tensor& scores = w.at("scores");
    if(static_cast<int>(logits.size()) != scores.dim(0))
        throw std::runtime_error("nardi_infer: out_dim != scores size (expected weighted-value head)");

    float max_logit = logits[0];
    for(float v : logits)
        if(v > max_logit)
            max_logit = v;

    double total = 0.0;
    std::vector<double> probs(logits.size());
    for(size_t i = 0; i < logits.size(); ++i)
    {
        probs[i] = std::exp(static_cast<double>(logits[i] - max_logit));
        total += probs[i];
    }

    double value = 0.0;
    for(size_t i = 0; i < probs.size(); ++i)
        value += (probs[i] / total) * static_cast<double>(scores.data[i]);

    return static_cast<float>(value);
}

// Split a filled [6, 25] feature block into the [6, 24] board (channel-major)
// and the 6 trailing scalars.
void split_board_scalars(const float* feat, std::vector<float>& board, float scalars[FEAT_ROWS])
{
    board.resize(static_cast<size_t>(FEAT_ROWS) * BOARD_COLS);
    for(int c = 0; c < FEAT_ROWS; ++c)
    {
        for(int p = 0; p < BOARD_COLS; ++p)
            board[static_cast<size_t>(c) * BOARD_COLS + p] = feat[c * FEAT_COLS + p];
        scalars[c] = feat[c * FEAT_COLS + (FEAT_COLS - 1)];
    }
}

// ---- concrete networks -------------------------------------------------

template <typename Derived>
class NetBase : public InferenceNet
{
public:
    float evaluate(const Nardi::Board::Features& f) const override
    {
        return static_cast<const Derived*>(this)->forward(f);
    }

    std::vector<float> evaluate_batch(
        const std::vector<Nardi::Board::Features>& features) const override
    {
        std::vector<float> out(features.size());
        for(size_t i = 0; i < features.size(); ++i)
            out[i] = static_cast<const Derived*>(this)->forward(features[i]);
        return out;
    }
};

// NardiNet: flatten [6,25] -> 150 -> trunk.
class MlpNet : public NetBase<MlpNet>
{
public:
    explicit MlpNet(Blob blob) : _w(std::move(blob)) {}

    float forward(const Nardi::Board::Features& f) const
    {
        float feat[FEAT_ROWS * FEAT_COLS];
        fill_features(f, feat, ModelKind::LEGACY);
        std::vector<float> x(feat, feat + FEAT_ROWS * FEAT_COLS);
        return trunk_value(std::move(x), _w);
    }

private:
    Blob _w;
};

// ConvNardiNet: Conv1d(6->C, k=5) [+ ReLU + Conv1d(C->C, k=5, pad=2)] ->
// flatten -> LayerNorm -> ReLU -> concat scalars -> trunk.
class ConvNet : public NetBase<ConvNet>
{
public:
    explicit ConvNet(Blob blob)
        : _w(std::move(blob)), _extra_conv(_w.has("conv.0.weight"))
    {
    }

    float forward(const Nardi::Board::Features& f) const
    {
        float feat[FEAT_ROWS * FEAT_COLS];
        fill_features(f, feat, ModelKind::CONV);
        std::vector<float> board;
        float scalars[FEAT_ROWS];
        split_board_scalars(feat, board, scalars);

        std::vector<float> x;
        if(_extra_conv)
        {
            auto c = conv1d(board.data(), FEAT_ROWS, BOARD_COLS,
                            _w.at("conv.0.weight"), _w.at("conv.0.bias"), 1, 0);
            const int channels = _w.at("conv.0.weight").dim(0);
            const int len = static_cast<int>(c.size()) / channels;
            relu_inplace(c);
            x = conv1d(c.data(), channels, len,
                       _w.at("conv.2.weight"), _w.at("conv.2.bias"), 1, 2);
        }
        else
        {
            x = conv1d(board.data(), FEAT_ROWS, BOARD_COLS,
                       _w.at("conv.weight"), _w.at("conv.bias"), 1, 0);
        }

        layer_norm_inplace(x, _w.at("norm.weight"), _w.at("norm.bias"));
        relu_inplace(x);
        x.insert(x.end(), scalars, scalars + FEAT_ROWS);
        return trunk_value(std::move(x), _w);
    }

private:
    Blob _w;
    bool _extra_conv;
};

// ResNardiNet: residual block (conv1 -> ReLU -> conv2, plus 1x1 proj skip) ->
// flatten -> LayerNorm -> ReLU -> concat scalars -> trunk.
class ResNet : public NetBase<ResNet>
{
public:
    explicit ResNet(Blob blob) : _w(std::move(blob)) {}

    float forward(const Nardi::Board::Features& f) const
    {
        float feat[FEAT_ROWS * FEAT_COLS];
        fill_features(f, feat, ModelKind::RES);
        std::vector<float> board;
        float scalars[FEAT_ROWS];
        split_board_scalars(feat, board, scalars);

        auto c1 = conv1d(board.data(), FEAT_ROWS, BOARD_COLS,
                         _w.at("res_block.conv1.weight"), _w.at("res_block.conv1.bias"), 1, 2);
        const int conv_out = _w.at("res_block.conv1.weight").dim(0);
        relu_inplace(c1);

        auto c2 = conv1d(c1.data(), conv_out, BOARD_COLS,
                         _w.at("res_block.conv2.weight"), _w.at("res_block.conv2.bias"), 1, 2);

        auto proj = conv1d(board.data(), FEAT_ROWS, BOARD_COLS,
                           _w.at("res_block.proj.weight"), _w.at("res_block.proj.bias"), 1, 0);

        for(size_t i = 0; i < c2.size(); ++i)
            c2[i] += proj[i];
        relu_inplace(c2); // flattened [conv_out * 24], channel-major == torch flatten(1)

        layer_norm_inplace(c2, _w.at("norm.weight"), _w.at("norm.bias"));
        relu_inplace(c2);
        c2.insert(c2.end(), scalars, scalars + FEAT_ROWS);
        return trunk_value(std::move(c2), _w);
    }

private:
    Blob _w;
};

} // namespace

std::unique_ptr<InferenceNet> load_inference_net(const std::string& path)
{
    Blob blob = read_blob(path);
    switch(blob.kind)
    {
    case ModelKind::LEGACY:
        return std::make_unique<MlpNet>(std::move(blob));
    case ModelKind::CONV:
        return std::make_unique<ConvNet>(std::move(blob));
    case ModelKind::RES:
        return std::make_unique<ResNet>(std::move(blob));
    default:
        throw std::runtime_error("nardi_infer: unknown model kind in weight blob");
    }
}

} // namespace nardi_py
