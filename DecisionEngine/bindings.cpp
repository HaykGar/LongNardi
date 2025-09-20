// bindings.cpp
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <array>
#include <vector>
#include <stdexcept>

namespace py = pybind11;

// Your engine headers (inside namespace Nardi)
#include "../CoreEngine/ScenarioBuilder.h"
#include "../CoreEngine/Game.h"
#include "../CoreEngine/Controller.h"
#include "../CoreEngine/Auxilaries.h"

// A thin wrapper that owns a live Game and provides Python-friendly methods.
class NardiEngine {
public:
    NardiEngine()
    : _builder()
    {}

    Nardi::ScenarioBuilder& GetBuilder()
    {
        return _builder;
    }

    int dice_flattened(int i, int j)
    {
        if(i <= 0 || j <= 0 || i > 6 || j > 6){
            std::cout << "unexpected dice fed in\n"; 
            return -1;
        }

        if (i > j)
            std::swap(i, j);
        // i <= j
        switch(i)
        {
            case 1:
                return j - 1;
            case 2:
                return 6 + j - 2;
            case 3:
                return 11 + j - 3;
            case 4:
                return 15 + j - 4;
            case 5:
                return 18 + j - 5;
            case 6:
                return 20;
            default:
                std::cout << "unexpected dice fed in\n";
        }
    }

    std::array<int, 2> flat_to_dice (int flattened)
    {
        if(flattened < 0 || flattened > 20)
        {
            std::cout << "weirdness in unflattening dice\n";
            return {-1, -1};
        }
        else
        {
            if(flattened < 6)
                return {1, flattened + 1};
            else if (flattened < 11)
                return {2, flattened - 4};
            else if (flattened < 15)
                return {3, flattened - 8};
            else if (flattened < 18)
                return {4, flattened - 11};
            else if (flattened < 20)
                return {5, flattened - 13};
            else 
                return {6, 6};
        } 
    }

    py::array_t<uint8_t> dice() const
    {
        std::array<int, 2> dice = { _builder.GetGame().GetDice(0), _builder.GetGame().GetDice(1) };
        py::array_t<uint8_t> arr({py::ssize_t(2)});

        auto buf = arr.mutable_unchecked<1>();
        buf(0) = dice[0];
        buf(1) = dice[1];

        return arr;
    }

    // Return 2x25 uint8 (player-perspective)
    py::array_t<uint8_t> board_key() const {
        const Nardi::BoardKey key = _builder.GetGame().GetBoardAsKey();

        py::array_t<uint8_t> arr({py::ssize_t(2), py::ssize_t(25)});
        auto buf = arr.mutable_unchecked<2>();
        for (int i = 0; i < 25; ++i) {
            buf(0, i) = key[0][i];
            buf(1, i) = key[1][i];
        }

        return arr;
    }

    // Helper to print a 3D numpy array (uint8) as [N,2,25]
    void PrintArray3D(py::array_t<uint8_t>& arr) {
        auto buf = arr.unchecked<3>();
        auto shape = arr.shape();
        std::cout << "Array shape: [" << shape[0] << "," << shape[1] << "," << shape[2] << "]\n";
        for (ssize_t i = 0; i < shape[0]; ++i) {
            std::cout << "Index " << i << ":\n";
            for (ssize_t r = 0; r < shape[1]; ++r) {
                for (ssize_t c = 0; c < shape[2]; ++c) {
                    std::cout << (int)buf(i, r, c) << " ";
                }
                std::cout << "\n";
            }
            std::cout << "----------------------\n";
        }
    }

    // Roll dice & return only board keys as [N,2,25] uint8 (end-of-turn leaves).
    py::array_t<uint8_t> roll_and_enumerate() {
        const auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));

        return enumerate(status);
    }

    // Set dice & return only board keys as [N,2,25] uint8 (end-of-turn leaves).
    py::array_t<uint8_t> set_and_enumerate(int d1, int d2) 
    {
        std::array<int, 2> new_dice = {d1, d2};
        const auto status = _builder.ReceiveCommand(Nardi::Command(new_dice));

        return enumerate(status);
    }

    py::array_t<uint8_t> enumerate(Nardi::status_codes status)
    {
        if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT) {
            return py::array_t<uint8_t>({0, 2, 25});
        } else if (status != Nardi::status_codes::SUCCESS) {
            Nardi::DispErrorCode(status);
            throw std::runtime_error("RollDice: unexpected controller status (should never happen).");
        }

        // Success: get map<BoardKey, MoveSequence, ...>
        const auto& b2s = _builder.GetGame().GetBoards2Seqs();

        // Copy keys to a vector and sort for deterministic order (optional).
        std::vector<Nardi::BoardKey> keys;
        keys.reserve(b2s.size());
        for (const auto& kv : b2s) keys.push_back(kv.first);

        py::array_t<uint8_t> boards({py::ssize_t(keys.size()), py::ssize_t(2), py::ssize_t(25)});
        auto b = boards.mutable_unchecked<3>();
        for (size_t i = 0; i < keys.size(); ++i) {
            const auto& k = keys[i];
            for (int c = 0; c < 25; ++c) {
                b(i, 0, c) = k[0][c];
                b(i, 1, c) = k[1][c];
            }
        }

        return boards;
    }

    py::tuple children_to_grandchildren(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> children_np)
    {
        // --- validate & load children [C,2,25] -> native keys ---
        if (children_np.ndim() != 3 || children_np.shape(1) != 2 || children_np.shape(2) != 25) {
            throw std::runtime_error("children must be [C,2,25] uint8");
        }
        const int NUM_CHILDREN = static_cast<int>(children_np.shape(0));
        auto U = children_np.unchecked<3>();
        std::vector<Nardi::BoardKey> children(NUM_CHILDREN);
        for (int64_t c = 0; c < NUM_CHILDREN; ++c)
            for (int r = 0; r < 2; ++r)
                for (int s = 0; s < 25; ++s)
                    children[c][r][s] = U(c, r, s);

        // --- one-pass enumeration into per-(child,dice) buckets of BoardKey ---
        std::vector<std::vector<std::vector<Nardi::BoardKey>>> buckets(
            NUM_CHILDREN, std::vector<std::vector<Nardi::BoardKey>>(N_DICE_COMB));
        std::vector<std::vector<int64_t>> counts(
            NUM_CHILDREN, std::vector<int64_t>(N_DICE_COMB, 0));
        std::vector<std::vector<int>> wins_c_d(
            NUM_CHILDREN, std::vector<int>(N_DICE_COMB));   // 0 by default

        {
            py::gil_scoped_release no_gil;

            int n_th = std::min(32, NUM_CHILDREN);

            #pragma omp parallel for num_threads(n_th)
            for (int64_t ch_idx = 0; ch_idx < NUM_CHILDREN; ++ch_idx) 
            {
                Nardi::ScenarioBuilder d_bldr(_builder);                 // thread-local copy
                d_bldr.ReceiveCommand(Nardi::Command(children[ch_idx])); // move to child state - child's perspective
                d_bldr.GetCtrl().ToSimMode();
                for (int flat_dice = 0; flat_dice < N_DICE_COMB; ++flat_dice) 
                {    
                    std::array<int,2> dice = flat_to_dice(flat_dice);
                    const auto status = d_bldr.ReceiveCommand(Nardi::Command(dice));    // set dice -> game updates b2s
                    if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT) {
                        counts[ch_idx][flat_dice] = 0;
                        continue;
                    }
                    if (status != Nardi::status_codes::SUCCESS) {
                        Nardi::DispErrorCode(status);
                        throw std::runtime_error("get_grandchildren_grouped: unexpected controller status");
                    }

                    const auto& b2s = d_bldr.GetGame().GetBoards2Seqs();
                    auto& vec = buckets[ch_idx][flat_dice];
                    vec.reserve(b2s.size());
                    for (const auto& kv : b2s) 
                    {
                        if(kv.first[0][24] == Nardi::PIECES_PER_PLAYER) // winning move for child
                        {
                            int result = (kv.first[1][24] == 0) ? 2 : 1;
                            vec.clear();
                            wins_c_d[ch_idx][flat_dice] = result;
                            break;
                        }
                        vec.push_back(kv.first);       // collect BoardKey
                    }
                    counts[ch_idx][flat_dice] = static_cast<int64_t>(vec.size());   // 0 if win found
                }
            }
        }

        // --- compute total batch size & build counts matrix child_lo_np[C, D] ---
        int64_t grand_total = 0;
        for (int i = 0; i < NUM_CHILDREN; ++i)
            for (int j = 0; j < N_DICE_COMB; ++j)
                grand_total += counts[i][j];

        py::array_t<int64_t> child_lo_np({py::ssize_t(NUM_CHILDREN), py::ssize_t(N_DICE_COMB)});
        {
            auto M = child_lo_np.mutable_unchecked<2>();
            for (int i = 0; i < NUM_CHILDREN; ++i)
                for (int j = 0; j < N_DICE_COMB; ++j)
                    M(i, j) = counts[i][j];
        }

        // --- flatten buckets into a single boards[N,2,25] in child-major then dice-major order ---
        py::array_t<uint8_t> boards({py::ssize_t(grand_total), py::ssize_t(2), py::ssize_t(25)});
        {
            auto B = boards.mutable_unchecked<3>();
            int64_t row = 0;
            for (int64_t c = 0; c < NUM_CHILDREN; ++c) {
                for (int d = 0; d < N_DICE_COMB; ++d) {
                    const auto& vec = buckets[c][d];
                    for (const auto& key : vec) {
                        for (int col = 0; col < 25; ++col) {
                            B(row, 0, col) = key[0][col];
                            B(row, 1, col) = key[1][col];
                        }
                        ++row;
                    }
                }
            }
        }

        py::array_t<uint8_t> win_results({py::ssize_t(NUM_CHILDREN), py::ssize_t(N_DICE_COMB)});
        {
            auto W = win_results.mutable_unchecked<2>();
            for(int i = 0; i < NUM_CHILDREN; ++i)
                for(int j = 0; j < N_DICE_COMB; ++j)
                    W(i, j) = wins_c_d[i][j];
        }

        // return big batch + counts matrix (compute probs in Python)
        return py::make_tuple(boards, child_lo_np, win_results);
    }


    // Expects [2,25] uint8, converts to BoardKey, applies the sequence via controller.
    void apply_board(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> arr) {
        if (arr.ndim() != 2 || arr.shape(0) != 2 || arr.shape(1) != 25) {
            throw std::runtime_error("apply_board: expected uint8 array of shape [2,25] (as returned by roll_and_enumerate)");
        }
        Nardi::BoardKey key{}; // two rows
        auto v = arr.unchecked<2>();
        for (size_t c = 0; c < 25; ++c) {
            key[0][c] = v(0, c);
            key[1][c] = v(1, c);
        }
        _builder.ReceiveCommand(Nardi::Command(key));
    }

    void with_sim_mode()
    {
        _builder.GetCtrl().ToSimMode();
    }

    void end_sim_mode()
    {
        _builder.GetCtrl().EndSimMode();
    }

    bool step_forward(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> arr)
    {
        if(_builder.GetCtrl().InSimMode())
        {
            apply_board(arr);
            return _builder.GetCtrl().AdvanceSimTurn();
        }
        return false;
    }

    bool step_back()
    {
        if(_builder.GetCtrl().InSimMode())
        {
            Nardi::status_codes result = _builder.GetCtrl().ReceiveCommand(Nardi::Actions::UNDO);
            return true;
        }
        return false;
    }

    bool is_terminal() const {
        return _builder.GetGame().GameIsOver();
    }

    int winner_result() const {
        if (!_builder.GetGame().GameIsOver())
            throw std::runtime_error("winner_result(): game not over");
        return _builder.GetGame().IsMars() ? 2 : 1;
    }

    void reset() {
        _builder.Reset();
    }

    void status_report()
    {
        _builder.StatusReport();
    }

    std::string status_str()
    {
        return _builder.StatusString();
    }

private:
    static constexpr int N_DICE_COMB = 21;
    Nardi::ScenarioBuilder _builder;
};

// ---- pybind11 module ----
PYBIND11_MODULE(nardi, m) 
{
    m.doc() = "Py bindings for Nardi C++ engine (Python owns the loop)";

    py::class_<NardiEngine>(m, "Engine")
        .def(py::init<>())
        .def("board_key",           &NardiEngine::board_key,
             R"(Return 2x25 uint8 board (player-perspective).)")
        .def("dice",                &NardiEngine::dice,
             R"(Return 1x2 uint8 dice values.)")
        .def("flat_to_dice",        &NardiEngine::flat_to_dice, 
             R"(Input: int index for flattened dice combo representation. Output: int array length 2, representing dice1, dice2.)")
        .def("roll_and_enumerate",  &NardiEngine::roll_and_enumerate,
             R"(Roll dice and return uint8 array of shape [N,2,25] with end-of-turn boards.)")
        .def("apply_board",         &NardiEngine::apply_board, py::arg("key"),
             R"(Apply the sequence that reaches the provided board key [2,25] uint8.)")
        .def("children_to_grandchildren", &NardiEngine::children_to_grandchildren, py::arg("children"),
             R"(Given children, return grandchildren for 1-ply lookahead evaluation.)")
        .def("with_sim_mode",       &NardiEngine::with_sim_mode,
             R"(Set controller sim mode true)")
        .def("end_sim_mode",        &NardiEngine::end_sim_mode,
             R"(Set controller sim mode true)")
        .def("step_forward",        &NardiEngine::step_forward, py::arg("key"),
             R"(In simulation mode, apply the sequence that reaches the provided board key [2,25] uint8, return whether successful)")
        .def("step_back",           &NardiEngine::step_back,
             R"(In simulation mode, undo the last full turn sequence, return whether successful)")
        .def("set_and_enumerate",   &NardiEngine::set_and_enumerate,
             R"(Set dice and return uint8 array of shape [N,2,25] with end-of-turn boards.)")
        .def("status_report",       &NardiEngine::status_report)
        .def("status_str",          &NardiEngine::status_str)
        .def("is_terminal",         &NardiEngine::is_terminal)
        .def("winner_result",       &NardiEngine::winner_result)
        .def("reset",               &NardiEngine::reset);
}