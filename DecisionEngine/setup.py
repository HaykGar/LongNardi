from setuptools import setup, Extension
import pybind11

# The C++ extension no longer links against LibTorch: model inference is now a
# hand-rolled, dependency-free implementation (nardi_infer.{h,cpp}) that loads a
# flat weight blob exported by nardi_net.export_weights. PyTorch remains a pure
# Python dependency for training and for parity tests, but is not compiled in.

ext = Extension(
    name="nardi",
    sources=[
        "bindings.cpp",
        "binding_utils.cpp",
        "lookahead_batch.cpp",
        "mcts_node.cpp",
        "nardi_c_api.cpp",
        "nardi_core.cpp",
        "nardi_engine.cpp",
        "nardi_infer.cpp",
        "python_views.cpp",
        "scenario_config.cpp",
        "target_model.cpp",
        "../CoreEngine/Auxilaries.cpp",
        "../CoreEngine/Board.cpp",
        "../CoreEngine/Controller.cpp",
        "../CoreEngine/Game.cpp",
        "../CoreEngine/Monitors.cpp",
        "../CoreEngine/ReaderWriter.cpp",
        "../CoreEngine/TerminalRW.cpp",
        "../CoreEngine/SFMLRW.cpp",
        "../CoreEngine/ScenarioBuilder.cpp",
    ],
    include_dirs=[
        "/opt/homebrew/include",
        pybind11.get_include(),
        ".",  # project root
    ],
    library_dirs=[
        "/opt/homebrew/lib",
    ],
    libraries=[
        "sfml-graphics",
        "sfml-window",
        "sfml-system",
    ],
    # The Python/desktop module keeps the SFML graphical view; the iOS build
    # compiles without it (renders via SwiftUI).
    define_macros=[("NARDI_ENABLE_SFML", None)],
    language="c++",
    extra_compile_args=["-std=c++23", "-mmacosx-version-min=10.15"],
    extra_link_args=[
        "-mmacosx-version-min=10.15",
        "-Wl,-rpath,/opt/homebrew/lib",
    ],
)

setup(
    name="nardi",
    version="1.0",
    ext_modules=[ext],
)
