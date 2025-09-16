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
        "../CoreEngine/ScenarioBuilder.cpp",
    ],
    include_dirs=[
        pybind11.get_include(),
        ".",  # project root
    ],
    language="c++",
    extra_compile_args=["-std=c++23"]
)

setup(
    name="nardi",
    version="0.1",
    ext_modules=[ext],
)
