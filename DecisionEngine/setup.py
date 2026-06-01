from setuptools import setup, Extension
import pybind11
import torch
from torch.utils import cpp_extension

torch_library_dirs = cpp_extension.library_paths()
torch_libraries = ["torch", "torch_cpu", "c10"]
torch_rpaths = [f"-Wl,-rpath,{path}" for path in torch_library_dirs]

ext = Extension(
    name="nardi",
    sources=[
        "bindings.cpp",
        "binding_utils.cpp",
        "lookahead_batch.cpp",
        "mcts_node.cpp",
        "nardi_engine.cpp",
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
        *cpp_extension.include_paths(),
        ".",  # project root
    ],
    library_dirs=[
        "/opt/homebrew/lib",
        *torch_library_dirs,
    ],
    libraries=[
        "sfml-graphics",
        "sfml-window",
        "sfml-system",
        *torch_libraries,
    ],
    language="c++",
    extra_compile_args=["-std=c++23", "-mmacosx-version-min=10.15"],
    extra_link_args=[
    "-mmacosx-version-min=10.15",
    "-Wl,-rpath,/opt/homebrew/lib",
    *torch_rpaths,
    ]
)

setup(
    name="nardi",
    version="1.0",
    ext_modules=[ext],
)
