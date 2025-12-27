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
#include "../CoreEngine/ReaderWriter.h"
#include "../CoreEngine/TerminalRW.h"
#include "../CoreEngine/SFMLRW.h"


/*

ScenarioConfig exposes a safer view of ScenarioBuilder which only crafts scenarios without messing 
with internals like RW pointers and other potentially error-causing things

*/

class ScenarioConfig
{
    public:
        ScenarioConfig(Nardi::ScenarioBuilder& sb) : _builder(sb) {}

        Nardi::status_codes withScenario(bool p_idx, const Nardi::BoardConfig& b, int d1, int d2, int d1u=0, int d2u=0)
        {
            return _builder.withScenario(p_idx, b, d1, d2, d1u, d2u);
        }

        Nardi::status_codes withScenario(
            bool p_idx, 
            py::array_t<int8_t, py::array::c_style | py::array::forcecast> board,
            int d1, int d2, int d1u = 0, int d2u = 0)
        {
            if (board.ndim() != 2 || board.shape(0) != Nardi::ROWS || board.shape(1) != Nardi::COLS)
                throw std::runtime_error("BoardConfig must be shape (2, 12)");

            Nardi::BoardConfig cfg;
            auto buf = board.unchecked<2>();

            for (size_t r = 0; r < Nardi::ROWS; ++r)
                for (size_t c = 0; c < Nardi::COLS; ++c)
                    cfg[r][c] = buf(r, c);

            return withScenario(p_idx, cfg, d1, d2, d1u, d2u);
        }

        Nardi::status_codes withDice(int d1, int d2, int d1_used, int d2_used)
        {
            return _builder.withDice(d1, d2, d1_used, d2_used);
        }        

        void withRandomEndgame(bool p_idx)
        {
            _builder.withRandomEndgame(p_idx);
        }    

        void withFirstTurn() { _builder.withFirstTurn(); }

        void Reset() { _builder.Reset(); }

    private:
        Nardi::ScenarioBuilder& _builder;
};

// A thin wrapper that owns a live Game and provides Python-friendly methods.
class NardiEngine {
public:
    NardiEngine()
    : _builder(), _config(_builder)
    {
        _builder.withFirstTurn();
    }

    ScenarioConfig& GetConfig() 
    { return _config; }

    void AttachNewTRW()
    {
        _builder.AttachNewRW(Nardi::TerminalRWFactory());
    }

    void AttachNewSFMLRW()
    {
        _builder.AttachNewRW(Nardi::SFMLRWFactory());
    }

    void DetachRW()
    {
        _builder.DetachRW();
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

    // Return 6x25 uint8 (player-perspective)

    Nardi::Board::Features board_features() const {
        return _builder.GetGame().GetBoardRef().ExtractFeatures();
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

    void Render() {

        _builder.Render();
    }

    // consider renaming for clarity... maybe just store the last status received from any command in a member?
    bool roll() {
        auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));
        return status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT;
    }

    // Roll dice & return only board keys as [N,6,25] uint8 (end-of-turn leaves).
    std::vector<Nardi::Board::Features> roll_and_enumerate() {
        const auto status = _builder.ReceiveCommand(Nardi::Command(Nardi::Actions::ROLL_DICE));

        return enumerate(status);
    }

    // Set dice & return only board keys as [N,6,25] uint8 (end-of-turn leaves).
    std::vector<Nardi::Board::Features> set_and_enumerate(int d1, int d2) 
    {
        std::array<int, 2> new_dice = {d1, d2};
        const auto status = _builder.ReceiveCommand(Nardi::Command(new_dice));

        return enumerate(status);
    }

    void apply_board(const Nardi::BoardConfig& brd) 
    {
        auto status = _builder.ReceiveCommand(Nardi::Command(brd));   // will autoplay this board via controller
        if(status != Nardi::status_codes::NO_LEGAL_MOVES_LEFT){
            Nardi::DispErrorCode(status);
            throw std::runtime_error("Autoplay failed to complete");
        }
    }

    void human_turn() 
    {
        if(!_builder.GetView())
            throw std::runtime_error("Tried human moves without initializing view");
        
        _builder.GetView()->InstructionMessage("Awaiting command\n");
        
        while(true)
        {
            Nardi::status_codes status = _builder.GetView()->PollInput();
            if (status != Nardi::status_codes::WAITING)
                _builder.GetView()->DispErrorCode(status);

            if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT)
                break;
        }
    }

    bool is_terminal() const {
        return _builder.GetGame().GameIsOver();
    }

    int winner_result() const {
        if (!_builder.GetGame().GameIsOver())
            throw std::runtime_error("winner_result(): game not over"); // consider sentinel value or something
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

    bool should_continue_game()
    {
        return !(_builder.GetCtrl().QuitRequested() || _builder.GetGame().GameIsOver());
    }

private:
    static constexpr int N_DICE_COMB = 21;
    Nardi::ScenarioBuilder _builder;
    ScenarioConfig         _config;

    std::vector<Nardi::Board::Features> enumerate(Nardi::status_codes status)
    {
        if (status == Nardi::status_codes::NO_LEGAL_MOVES_LEFT) {
            return {};
        } else if (status != Nardi::status_codes::SUCCESS) {
            Nardi::DispErrorCode(status);
            throw std::runtime_error("RollDice: unexpected controller status (should never happen).");
        }

        // Success: get map<BoardConfig, MoveSequence, ...>
        const auto& b2s = _builder.GetGame().GetBoards2Seqs();

        std::vector<Nardi::Board::Features> features_vec;
        features_vec.reserve(b2s.size());
        for (const auto& kv : b2s)
            features_vec.push_back(_builder.GetGame().GetBoardRef().ExtractFeatures(kv.first));

        return features_vec;
    }

};

py::array_t<uint8_t> occ_view(const Nardi::Board::Features::PlayerBoardInfo& pi)
{
    return py::array_t<uint8_t>(
        {3, Nardi::ROWS*Nardi::COLS},               // shape

        // {row stride size in bytes, col stride size in bytes}
        {sizeof(uint8_t) * Nardi::ROWS*Nardi::COLS, sizeof(uint8_t)}, 

        pi.occ.data()->data(),                      // ptr to pi.occ[0][0], here same as &pi.occ[0][0]
        py::cast(&pi)                               // memory ownership with cpp object
    );
}

py::array_t<int8_t> raw_data_view(const Nardi::Board::Features& f)
{
    return py::array_t<int8_t>(
        {Nardi::ROWS, Nardi::COLS},
        {sizeof(uint8_t) * Nardi::COLS, sizeof(uint8_t)},
        f.raw_data.data()->data(),
        py::cast(&f)
    );
}


// ---- pybind11 module ----
PYBIND11_MODULE(nardi, m) 
{
    m.doc() = "Py bindings for Nardi C++ engine (Python owns the loop) and scenario config object for easier manipulation";

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
             R"(Prompt human user for move and return false if their turn is over, true otherwise)")

        .def("board_features",      &NardiEngine::board_features,
             R"(Return 6x25 uint8 board (player-perspective).)")
        .def("apply_board",         &NardiEngine::apply_board, py::arg("board"),
             R"(Apply the sequence that reaches the provided board state [2,12] uint8.)")
        .def("dice",                &NardiEngine::dice,
             R"(Return 1x2 uint8 dice values.)")    
        .def("roll",                &NardiEngine::roll,
             R"(Roll dice. Return false if no legal moves left)")
        .def("roll_and_enumerate",  &NardiEngine::roll_and_enumerate,
             R"(Roll dice and return uint8 array of shape [N,6,25] with end-of-turn boards.)")
        .def("set_and_enumerate",   &NardiEngine::set_and_enumerate,
             R"(Set dice and return uint8 array of shape [N,6,25] with end-of-turn boards.)")
        
        .def("status_report",       &NardiEngine::status_report)
        .def("status_str",          &NardiEngine::status_str)
        .def("is_terminal",         &NardiEngine::is_terminal)
        .def("winner_result",       &NardiEngine::winner_result)
        .def("reset",               &NardiEngine::reset)
        .def("should_continue_game",&NardiEngine::should_continue_game);

    py::class_<ScenarioConfig>(m, "ScenarioConfig")
        .def("withScenario",
            (Nardi::status_codes (ScenarioConfig::*)(
                bool,
                py::array_t<int8_t, py::array::c_style | py::array::forcecast>,
                int, int, int, int
            ))                      &ScenarioConfig::withScenario,
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
        .def_property_readonly("raw_data",  &raw_data_view);

    py::class_<Nardi::Board::Features::PlayerBoardInfo>(m, "PlayerBoardInfo")
        .def_readonly("pip_count",          &Nardi::Board::Features::PlayerBoardInfo::pip_count)
        .def_readonly("pieces_off",         &Nardi::Board::Features::PlayerBoardInfo::pieces_off)
        .def_readonly("pieces_not_reached", &Nardi::Board::Features::PlayerBoardInfo::pieces_not_reached)
        .def_readonly("sq_occ",             &Nardi::Board::Features::PlayerBoardInfo::sq_occ)
        .def_property_readonly("occ",       &occ_view);
}