#include <memory>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "binding_utils.h"
#include "lookahead_batch.h"
#include "nardi_engine.h"
#include "nardi_infer.h"
#include "python_views.h"
#include "scenario_config.h"
#include "../CoreEngine/Auxilaries.h"

namespace py = pybind11;
using namespace nardi_py;

namespace
{

// Thin Python-facing wrapper around the hand-rolled C++ inference net. Used to
// validate parity against the PyTorch models (see tests/test_infer_parity.py).
class PyInferenceNet
{
public:
    explicit PyInferenceNet(const std::string& path) : _net(load_inference_net(path)) {}

    float evaluate(const Nardi::Board::Features& f) const { return _net->evaluate(f); }

    std::vector<float> evaluate_batch(const std::vector<Nardi::Board::Features>& fs) const
    {
        return _net->evaluate_batch(fs);
    }

private:
    std::unique_ptr<InferenceNet> _net;
};

using FloatArray = py::array_t<float, py::array::c_style | py::array::forcecast>;

// Parse a 1D float numpy array into a std::vector (the engine/batch validate the
// length against their own expectations).
std::vector<float> to_float_vector(const FloatArray& values)
{
    if(values.ndim() != 1)
        throw std::runtime_error("Expected a 1D float array.");
    auto buf = values.unchecked<1>();
    std::vector<float> out(static_cast<size_t>(values.shape(0)));
    for(py::ssize_t i = 0; i < values.shape(0); ++i)
        out[static_cast<size_t>(i)] = buf(i);
    return out;
}

// numpy [2,12] int8 board -> BoardConfig.
Nardi::BoardConfig array_to_board(
    py::array_t<int8_t, py::array::c_style | py::array::forcecast> board)
{
    if(board.ndim() != 2 || board.shape(0) != Nardi::ROWS || board.shape(1) != Nardi::COLS)
        throw std::runtime_error("BoardConfig must be shape (2, 12)");
    Nardi::BoardConfig cfg;
    auto buf = board.unchecked<2>();
    for(size_t r = 0; r < Nardi::ROWS; ++r)
        for(size_t c = 0; c < Nardi::COLS; ++c)
            cfg[r][c] = buf(r, c);
    return cfg;
}

py::array_t<float> lb_child_values(const LookaheadBatch& b, const FloatArray& values)
{
    const std::vector<float> cv = b.child_values_vec(to_float_vector(values));
    py::array_t<float> arr(static_cast<py::ssize_t>(cv.size()));
    auto buf = arr.mutable_unchecked<1>();
    for(size_t i = 0; i < cv.size(); ++i)
        buf(static_cast<py::ssize_t>(i)) = cv[i];
    return arr;
}

int lb_best_index(const LookaheadBatch& b, const FloatArray& values)
{
    return b.best_index_values(to_float_vector(values));
}

py::array_t<int8_t> lb_best_board(const LookaheadBatch& b, const FloatArray& values)
{
    return board_to_array(b.children.at(static_cast<size_t>(
        b.best_index_values(to_float_vector(values)))).board);
}

} // namespace

PYBIND11_MODULE(nardi, m)
{
    m.doc() = "Py bindings for Nardi C++ engine (Python owns the loop) and scenario config object for easier manipulation";

    m.def("features_to_tensor",     &features_to_tensor,
          py::arg("features"),
          py::arg("kind") = "conv",
          py::arg("flatten") = false,
          R"(Convert one Features object to a model-ready float tensor buffer.)");
    m.def("feature_batch_to_tensor", &feature_batch_to_tensor,
          py::arg("features"),
          py::arg("kind") = "conv",
          py::arg("flatten") = false,
          R"(Convert a list of Features objects to a model-ready float tensor buffer.)");

    py::class_<NardiEngine>(m, "Engine")
        .def(py::init<>())
        .def("config",              &NardiEngine::GetConfig,
             py::return_value_policy::reference)
        .def("AttachNewTRW",        &NardiEngine::AttachNewTRW,
             R"(Attaches TerminalRW for terminal view of game state.)")
        .def("AttachNewSFMLRW",     &NardiEngine::AttachNewSFMLRW,
             R"(Attaches SFMLRW for graphical view of game state.)")
        .def("DetachRW",            &NardiEngine::DetachRW,
             R"(Detaches ReaderWriter removing any view of game.)")
        .def("Render",              &NardiEngine::Render,
             R"(If view attached, then display)")
        .def("human_turn",          &NardiEngine::human_turn,
             py::arg("dice_rolled") = false,
             R"(Prompt human user for move and return false if their turn is over, true otherwise)")
        .def("board_features",      &NardiEngine::board_features,
             R"(Return 6x25 uint8 board in player perspective.)")
        .def("apply_board",         &NardiEngine::apply_board,
             py::arg("board"),
             R"(Apply the sequence that reaches the provided board state [2,12] uint8.)")
        .def("current_player",      &NardiEngine::current_player,
             R"(Return the C++ current player index.)")
        .def("sign",                &NardiEngine::sign,
             R"(Return +1 for white-to-move and -1 for black-to-move.)")
        .def("turn_num",            &NardiEngine::turn_num,
             R"(Return total completed turns across both players.)")
        .def("player_turn_num",     &NardiEngine::player_turn_num,
             py::arg("player"),
             R"(Return completed turns for one player.)")
        .def("dice",
             [](const NardiEngine& eng)
             {
                 const auto d = eng.dice_values();
                 py::array_t<uint8_t> arr(py::ssize_t(2));
                 auto buf = arr.mutable_unchecked<1>();
                 buf(0) = static_cast<uint8_t>(d[0]);
                 buf(1) = static_cast<uint8_t>(d[1]);
                 return arr;
             },
             R"(Return 1x2 uint8 dice values.)")
        .def("dice_as_idx",         &NardiEngine::dice_as_idx,
             R"(Get dice pair as a flattened idx corresponding to DICE_COMBOS)")
        .def("roll_has_children",   &NardiEngine::roll_has_children,
             R"(Roll dice. Return false if no legal moves left)")
        .def("roll_and_enumerate",  &NardiEngine::roll_and_enumerate,
             R"(Roll dice and return end-of-turn board features.)")
        .def("set_and_enumerate",
             py::overload_cast<int, int>(&NardiEngine::set_and_enumerate),
             py::arg("d1"), py::arg("d2"),
             R"(Set dice and enumerate all legal end-of-turn positions.)")
        .def("get_children",        &NardiEngine::enumerate,
             R"(List all children given the current dice roll. Error if dice not yet rolled.)")
        .def("make_lookahead_batch",
             [](NardiEngine& eng)
             {
                 py::gil_scoped_release release;
                 return eng.MakeLookaheadBatch();
             },
             R"(Build a batched one-ply lookahead frontier for model evaluation.)")
        .def("apply_best_lookahead",
             [](NardiEngine& eng, const FloatArray& values)
             { eng.apply_best_lookahead(to_float_vector(values)); },
             py::arg("values"),
             R"(Apply the best child from the last lookahead batch.)")
        .def("apply_noisy_board",
             [](NardiEngine& eng, const FloatArray& values, float eps, float temperature)
             { eng.apply_noisy_board(to_float_vector(values), eps, temperature); },
             py::arg("values"),
             py::arg("eps"),
             py::arg("temperature"),
             R"(Sample and apply a cached child board using softmax(values / temperature) plus Dirichlet noise.)")
        .def("apply_greedy_board",
             [](NardiEngine& eng, const FloatArray& values)
             { eng.apply_greedy_board(to_float_vector(values)); },
             py::arg("values"),
             R"(Apply the highest-valued cached child board.)")
        .def("apply_random_board",  &NardiEngine::apply_random_board,
             R"(Apply a random cached child board.)")
        .def("apply_heuristic_board",&NardiEngine::apply_heuristic_board,
             R"(Apply the cached child board with highest current-player square occupancy.)")
        .def("greedy_choice_target",  &NardiEngine::greedy_choice_target,
             R"(Index into the cached children the greedy bot would play, evaluated by the
loaded target network in C++ (no move applied).)")
        .def("apply_greedy_target",   &NardiEngine::apply_greedy_target,
             R"(Play the greedy bot's move using the loaded target network (C++ evaluation).)")
        .def("lookahead_choice_target", &NardiEngine::lookahead_choice_target,
             R"(Index into the lookahead batch's children the 1-ply bot would play, evaluated
by the loaded target network in C++ (no move applied). Requires dice rolled.)")
        .def("apply_lookahead_target",&NardiEngine::apply_lookahead_target,
             R"(Play the 1-ply lookahead bot's move using the loaded target network (C++).)")
        .def("configure_players",     &NardiEngine::configure_players,
             py::arg("white"), py::arg("black"),
             R"(Set the per-player move Strategy (white = player idx 0, black = idx 1).)")
        .def("set_mcts_params",       &NardiEngine::set_mcts_params,
             py::arg("n_sims"), py::arg("temperature") = 1.0f, py::arg("exploratory") = false,
             py::arg("c_uct") = 0.1f, py::arg("dirichlet_eps") = 0.25f,
             py::arg("dirichlet_alpha") = 0.3f, py::arg("rollouts_per_leaf") = 0,
             R"(Configure MCTS search tunables used by the Mcts strategy in advance().)")
        .def("advance",               &NardiEngine::advance,
             R"(Advance the match one step: roll for the current player and either play a
bot move (BotMoved), report a human move is awaited (AwaitingHuman), report a
forced pass (TurnPassed), or report the game is over (GameOver).)")
        .def("current_options",       &NardiEngine::current_options,
             R"(The cached legal end-of-turn options (Features) for the current player.)")
        .def("legal_move_count",      &NardiEngine::legal_move_count,
             R"(Number of cached legal options for the current player.)")
        .def("apply_human_move",      &NardiEngine::apply_human_move,
             py::arg("idx"),
             R"(Apply the human-chosen legal option by index into current_options().)")
        .def("human_select",          &NardiEngine::human_select, py::arg("row"), py::arg("col"),
             R"(Select a source point (engine coords) for an incremental human move.)")
        .def("human_move_die",        &NardiEngine::human_move_die, py::arg("die_idx"),
             R"(Move the selected piece by die 0/1; True if applied.)")
        .def("human_undo",            &NardiEngine::human_undo,
             R"(Undo the last sub-move in the current turn.)")
        .def("can_use_die",           &NardiEngine::can_use_die, py::arg("die_idx"))
        .def("start_is_selected",     &NardiEngine::start_is_selected)
        .def("selected_start",        &NardiEngine::selected_start)
        .def("turn_in_progress",      &NardiEngine::turn_in_progress)
        .def("status_report",       &NardiEngine::status_report)
        .def("status_str",          &NardiEngine::status_str)
        .def("is_terminal",         &NardiEngine::is_terminal)
        .def("winner_result",       &NardiEngine::winner_result)
        .def("reset",               &NardiEngine::reset)
        .def("should_continue_game",&NardiEngine::should_continue_game)
        .def("restart_or_quit",     &NardiEngine::restart_or_quit)
        .def("quit_requested",      &NardiEngine::quit_requested)
        .def("load_target_network", &NardiEngine::load_target_network,
             py::arg("path"),
             R"(Load a hand-rolled value-network weight blob for C++ MCTS self-play.)")
        .def("debug_target_eval", &NardiEngine::debug_target_eval)
        .def("run_mcts_game",
             [](NardiEngine& eng, int n_sims, float temperature, int max_turns,
                float c_uct, float dirichlet_eps, float dirichlet_alpha, int rollouts_per_leaf)
             {
                 py::gil_scoped_release release;
                 return eng.run_mcts_game(n_sims, temperature, max_turns,
                                          c_uct, dirichlet_eps, dirichlet_alpha, rollouts_per_leaf);
             },
             py::arg("n_sims"),
             py::arg("temperature") = 1.0f,
             py::arg("max_turns") = 1000,
             py::arg("c_uct") = 0.1f,
             py::arg("dirichlet_eps") = 0.25f,
             py::arg("dirichlet_alpha") = 0.3f,
             py::arg("rollouts_per_leaf") = 0,
             R"(Run one MCTS self-play game and return (Features, target) pairs.

The played-move (implicit) policy is Boltzmann exploration over visit counts at
the given temperature, mixed with Dirichlet(dirichlet_alpha) noise weighted by
dirichlet_eps. Set dirichlet_eps=0 to disable the noise. c_uct scales the UCT
exploration term during search.)")
        .def("mcts_apply_move",
             [](NardiEngine& eng, int n_sims, float temperature, bool exploratory,
                float c_uct, float dirichlet_eps, float dirichlet_alpha, int rollouts_per_leaf)
             {
                 py::gil_scoped_release release;
                 eng.mcts_apply_move(n_sims, temperature, exploratory,
                                     c_uct, dirichlet_eps, dirichlet_alpha, rollouts_per_leaf);
             },
             py::arg("n_sims"),
             py::arg("temperature") = 1.0f,
             py::arg("exploratory") = false,
             py::arg("c_uct") = 0.1f,
             py::arg("dirichlet_eps") = 0.25f,
             py::arg("dirichlet_alpha") = 0.3f,
             py::arg("rollouts_per_leaf") = 0,
             R"(MCTS move strategy: with dice already rolled, search from the current
position and apply the chosen move. exploratory=False (eval) plays the most-visited
move (model-informed UCT); exploratory=True (train) samples Boltzmann+Dirichlet.)");

    py::class_<ScenarioConfig>(m, "ScenarioConfig")
        .def("withScenario",
            [](ScenarioConfig& cfg, bool p_idx,
               py::array_t<int8_t, py::array::c_style | py::array::forcecast> board,
               int d1, int d2, int d1u, int d2u)
            { return cfg.withScenario(p_idx, array_to_board(board), d1, d2, d1u, d2u); },
            py::arg("p_idx"),
            py::arg("board"),
            py::arg("d1"),
            py::arg("d2"),
            py::arg("d1u") = 0,
            py::arg("d2u") = 0)
        .def("withDice",            &ScenarioConfig::withDice,
             py::arg("d1"),
             py::arg("d2"),
             py::arg("d1_used") = 0,
             py::arg("d2_used") = 0)
        .def("withRandomEndgame",   &ScenarioConfig::withRandomEndgame,
             py::arg("p_idx") = false);

    py::enum_<Strategy>(m, "Strategy")
        .value("Human",     Strategy::Human)
        .value("Greedy",    Strategy::Greedy)
        .value("Lookahead", Strategy::Lookahead)
        .value("Mcts",      Strategy::Mcts)
        .value("Heuristic", Strategy::Heuristic)
        .value("Random",    Strategy::Random);

    py::enum_<StepResult>(m, "StepResult")
        .value("GameOver",      StepResult::GameOver)
        .value("AwaitingHuman", StepResult::AwaitingHuman)
        .value("BotMoved",      StepResult::BotMoved)
        .value("TurnPassed",    StepResult::TurnPassed);

    py::enum_<Nardi::status_codes>(m, "status_codes")
        .value("SUCCESS",               Nardi::status_codes::SUCCESS)
        .value("NO_LEGAL_MOVES_LEFT",   Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
        .value("OUT_OF_BOUNDS",         Nardi::status_codes::OUT_OF_BOUNDS)
        .value("START_EMPTY_OR_ENEMY",  Nardi::status_codes::START_EMPTY_OR_ENEMY)
        .value("DEST_ENEMY",            Nardi::status_codes::DEST_ENEMY)
        .value("BACKWARDS_MOVE",        Nardi::status_codes::BACKWARDS_MOVE)
        .value("NO_PATH",               Nardi::status_codes::NO_PATH)
        .value("PREVENTS_COMPLETION",   Nardi::status_codes::PREVENTS_COMPLETION)
        .value("BAD_BLOCK",             Nardi::status_codes::BAD_BLOCK)
        .value("DICE_USED_ALREADY",     Nardi::status_codes::DICE_USED_ALREADY)
        .value("HEAD_PLAYED_ALREADY",   Nardi::status_codes::HEAD_PLAYED_ALREADY)
        .value("MISC_FAILURE",          Nardi::status_codes::MISC_FAILURE)
        .export_values();

    py::class_<Nardi::Board::Features>(m, "Features")
        .def_readonly("player",             &Nardi::Board::Features::player)
        .def_readonly("opp",                &Nardi::Board::Features::opp)
        .def_property_readonly("raw_data",  &raw_data_view)
        .def("swap_perspective",            &Nardi::Board::Features::SwapPerspective);

    py::class_<Nardi::Board::Features::PlayerBoardInfo>(m, "PlayerBoardInfo")
        .def_readonly("pip_count",          &Nardi::Board::Features::PlayerBoardInfo::pip_count)
        .def_readonly("pieces_off",         &Nardi::Board::Features::PlayerBoardInfo::pieces_off)
        .def_readonly("pieces_not_reached", &Nardi::Board::Features::PlayerBoardInfo::pieces_not_reached)
        .def_readonly("sq_occ",             &Nardi::Board::Features::PlayerBoardInfo::sq_occ)
        .def_property_readonly("occ",       &occ_view);

    py::class_<NardiEngine::ChanceAndChildren>(m, "ChanceAndChildren")
        .def_readonly("prob",               &NardiEngine::ChanceAndChildren::prob)
        .def_readonly("data",               &NardiEngine::ChanceAndChildren::data);

    py::class_<NardiEngine::Node, std::shared_ptr<NardiEngine::Node>>(m, "Node")
        .def("is_leaf",                     &NardiEngine::Node::isLeaf)
        .def_readonly("result",             &NardiEngine::Node::result)
        .def_readonly("features",           &NardiEngine::Node::features)
        .def_readonly("children_by_dice",   &NardiEngine::Node::children_by_dice);

    py::class_<PyInferenceNet>(m, "InferenceNet")
        .def(py::init<const std::string&>(), py::arg("path"),
             R"(Load a hand-rolled value network from a weight blob exported by
nardi_net.export_weights. Torch-free; mirrors model(features) in Python.)")
        .def("evaluate", &PyInferenceNet::evaluate, py::arg("features"),
             R"(Side-to-move value for one Features object.)")
        .def("evaluate_batch", &PyInferenceNet::evaluate_batch, py::arg("features"),
             R"(Side-to-move values for a list of Features objects.)");

    py::class_<LookaheadBatch, std::shared_ptr<LookaheadBatch>>(m, "LookaheadBatch")
        .def_property_readonly("num_children",      &LookaheadBatch::num_children)
        .def_property_readonly("num_eval_features", &LookaheadBatch::num_eval_features)
        .def_property_readonly("eval_features",
             [](const LookaheadBatch& b) { return b.eval_features; },
             R"(The re-featured grandchild positions (list of Features) the model scores.)")
        .def("tensor",
             [](const LookaheadBatch& b, const std::string& kind, bool flatten)
             { return feature_batch_to_tensor(b.eval_features, kind, flatten); },
             py::arg("kind") = "conv",
             py::arg("flatten") = false,
             R"(Return model-ready eval features as [N,6,25] or [N,150].)")
        .def("child_values",                        &lb_child_values,
             py::arg("values"),
             R"(Aggregate leaf values into one value per legal child move.)")
        .def("best_index",                          &lb_best_index,
             py::arg("values"),
             R"(Return the argmax child move index after value aggregation.)")
        .def("best_board",                          &lb_best_board,
             py::arg("values"),
             R"(Return the raw board for the argmax child move.)");
}
