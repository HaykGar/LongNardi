#include <memory>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "binding_utils.h"
#include "lookahead_batch.h"
#include "nardi_engine.h"
#include "python_views.h"
#include "scenario_config.h"
#include "../CoreEngine/Auxilaries.h"

namespace py = pybind11;
using namespace nardi_py;

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
        .def("dice",                &NardiEngine::dice,
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
        .def("apply_best_lookahead",&NardiEngine::apply_best_lookahead,
             py::arg("values"),
             R"(Apply the best child from the last lookahead batch.)")
        .def("apply_noisy_board",   &NardiEngine::apply_noisy_board,
             py::arg("values"),
             py::arg("eps"),
             py::arg("temperature"),
             R"(Sample and apply a cached child board using softmax(values / temperature) plus Dirichlet noise.)")
        .def("apply_greedy_board",  &NardiEngine::apply_greedy_board,
             py::arg("values"),
             R"(Apply the highest-valued cached child board.)")
        .def("apply_random_board",  &NardiEngine::apply_random_board,
             R"(Apply a random cached child board.)")
        .def("apply_heuristic_board",&NardiEngine::apply_heuristic_board,
             R"(Apply the cached child board with highest current-player square occupancy.)")
        .def("status_report",       &NardiEngine::status_report)
        .def("status_str",          &NardiEngine::status_str)
        .def("is_terminal",         &NardiEngine::is_terminal)
        .def("winner_result",       &NardiEngine::winner_result)
        .def("reset",               &NardiEngine::reset)
        .def("should_continue_game",&NardiEngine::should_continue_game)
        .def("restart_or_quit",     &NardiEngine::restart_or_quit)
        .def("quit_requested",      &NardiEngine::quit_requested);

    py::class_<ScenarioConfig>(m, "ScenarioConfig")
        .def("withScenario",
            static_cast<Nardi::status_codes (ScenarioConfig::*)(
                bool,
                py::array_t<int8_t, py::array::c_style | py::array::forcecast>,
                int, int, int, int
            )>(&ScenarioConfig::withScenario),
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

    py::class_<LookaheadBatch, std::shared_ptr<LookaheadBatch>>(m, "LookaheadBatch")
        .def_property_readonly("num_children",      &LookaheadBatch::num_children)
        .def_property_readonly("num_eval_features", &LookaheadBatch::num_eval_features)
        .def("tensor",                              &LookaheadBatch::tensor,
             py::arg("kind") = "conv",
             py::arg("flatten") = false,
             R"(Return model-ready eval features as [N,6,25] or [N,150].)")
        .def("child_values",                        &LookaheadBatch::child_values,
             py::arg("values"),
             R"(Aggregate leaf values into one value per legal child move.)")
        .def("best_index",                          &LookaheadBatch::best_index,
             py::arg("values"),
             R"(Return the argmax child move index after value aggregation.)")
        .def("best_board",                          &LookaheadBatch::best_board,
             py::arg("values"),
             R"(Return the raw board for the argmax child move.)");
}
