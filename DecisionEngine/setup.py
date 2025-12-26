from setuptools import setup, Extension
import pybind11

ext = Extension(
    name="nardi",
    sources=[
        "bindings.cpp",
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
    language="c++",
    extra_compile_args=["-std=c++23", "-mmacosx-version-min=10.15"],
    extra_link_args=[
    "-mmacosx-version-min=10.15",
    "-Wl,-rpath,/opt/homebrew/lib",
    ]
)

setup(
    name="nardi",
    version="1.0",
    ext_modules=[ext],
)
