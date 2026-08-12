/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <Python.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perception.h"
#include "python_bridge/perception_python_bridge.h"

#ifndef PERCEPTION_PYTHON_SDK_PATH
#define PERCEPTION_PYTHON_SDK_PATH ""
#endif

#ifndef PERCEPTION_PYTHON_EXECUTABLE
#define PERCEPTION_PYTHON_EXECUTABLE ""
#endif

namespace {

constexpr int EXIT_USAGE = 2;
constexpr int EXIT_PYTHON_SETUP = 3;
constexpr int EXIT_SCRIPT = 4;
constexpr int EXIT_OUTPUT = 5;

class PythonInitializationError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class ScriptReadError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class PyObjectPtr {
  public:
    explicit PyObjectPtr(PyObject *object = nullptr) noexcept : object_(object) {}

    PyObjectPtr(const PyObjectPtr &) = delete;
    PyObjectPtr &operator=(const PyObjectPtr &) = delete;

    PyObjectPtr(PyObjectPtr &&other) noexcept : object_(std::exchange(other.object_, nullptr)) {}

    PyObjectPtr &operator=(PyObjectPtr &&other) noexcept {
        if (this != &other) {
            Py_XDECREF(object_);
            object_ = std::exchange(other.object_, nullptr);
        }
        return *this;
    }

    ~PyObjectPtr() {
        Py_XDECREF(object_);
    }

    [[nodiscard]] PyObject *get() const noexcept {
        return object_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return object_ != nullptr;
    }

  private:
    PyObject *object_;
};

class PythonRuntime {
  public:
    PythonRuntime() {
        perception::python_bridge::append_inittab();

        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        PyStatus status =
            PyConfig_SetBytesString(&config, &config.program_name, PERCEPTION_PYTHON_EXECUTABLE);
        if (!PyStatus_Exception(status)) {
            status = Py_InitializeFromConfig(&config);
        }

        const std::string error = status.err_msg != nullptr ? status.err_msg : "unknown error";
        PyConfig_Clear(&config);
        if (PyStatus_Exception(status) || !Py_IsInitialized()) {
            throw PythonInitializationError("failed to initialize embedded Python: " + error);
        }
    }

    PythonRuntime(const PythonRuntime &) = delete;
    PythonRuntime &operator=(const PythonRuntime &) = delete;

    ~PythonRuntime() {
        if (Py_IsInitialized()) {
            Py_FinalizeEx();
        }
    }
};

struct Options {
    std::filesystem::path output;
    std::vector<std::filesystem::path> pythonPaths;
    std::vector<std::filesystem::path> scripts;
};

void printUsage(std::ostream &stream, std::string_view executable) {
    stream << "usage: " << executable
           << " --output <packet.bin> [--python-path <directory>]... "
              "<script.py> [<script.py> ...]\n";
}

bool parseOptions(std::span<char *> arguments, Options &options) {
    bool positionalOnly = false;
    size_t index = 1;

    while (index < arguments.size()) {
        const std::string_view argument(arguments[index]);
        ++index;
        if (!positionalOnly && argument == "--") {
            positionalOnly = true;
        } else if (!positionalOnly && argument == "--output") {
            if (index >= arguments.size()) {
                std::cerr << "--output requires a path\n";
                return false;
            }
            options.output = arguments[index];
            ++index;
        } else if (!positionalOnly && argument == "--python-path") {
            if (index >= arguments.size()) {
                std::cerr << "--python-path requires a directory\n";
                return false;
            }
            options.pythonPaths.emplace_back(arguments[index]);
            ++index;
        } else if (!positionalOnly && !argument.empty() && argument.front() == '-') {
            std::cerr << "unknown option: " << argument << '\n';
            return false;
        } else {
            options.scripts.emplace_back(argument);
        }
    }

    if (options.output.empty()) {
        std::cerr << "--output is required\n";
        return false;
    }
    if (options.scripts.empty()) {
        std::cerr << "at least one guest script is required\n";
        return false;
    }
    return true;
}

std::string readScript(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ScriptReadError("failed to open guest script: " + path.string());
    }

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool prependSysPath(const std::filesystem::path &path) {
    PyObjectPtr sys(PyImport_ImportModule("sys"));
    PyObjectPtr sysPath(sys ? PyObject_GetAttrString(sys.get(), "path") : nullptr);
    const auto pathString = std::filesystem::absolute(path).string();
    PyObjectPtr item(PyUnicode_FromString(pathString.c_str()));
    if (!sys || !sysPath || !PyList_Check(sysPath.get()) || !item) {
        PyErr_Print();
        return false;
    }

    if (PyList_Insert(sysPath.get(), 0, item.get()) < 0) {
        PyErr_Print();
        return false;
    }
    return true;
}

bool configurePythonPaths(const Options &options) {
    std::vector<std::filesystem::path> paths;
    paths.emplace_back(PERCEPTION_PYTHON_SDK_PATH);
    paths.insert(paths.end(), options.pythonPaths.begin(), options.pythonPaths.end());
    for (const auto &script : options.scripts) {
        paths.push_back(std::filesystem::absolute(script).parent_path());
    }

    for (auto iterator = paths.rbegin(); iterator != paths.rend(); ++iterator) {
        if (!std::filesystem::is_directory(*iterator)) {
            std::cerr << "Python path is not a directory: " << iterator->string() << '\n';
            return false;
        }
        if (!prependSysPath(*iterator)) {
            return false;
        }
    }
    return true;
}

bool executeScript(const std::filesystem::path &path, size_t index, PyObject *envelope) {
    std::string source;
    try {
        source = readScript(path);
    } catch (const ScriptReadError &error) {
        std::cerr << error.what() << '\n';
        return false;
    }
    const auto absolutePath = std::filesystem::absolute(path);
    const auto moduleName = std::format("_pek_guest_script_{}", index);

    PyObjectPtr pythonModule(PyModule_New(moduleName.c_str()));
    if (!pythonModule) {
        std::cerr << "failed to create module for guest script: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }
    if (PyDict_SetItemString(PyImport_GetModuleDict(), moduleName.c_str(), pythonModule.get()) <
        0) {
        std::cerr << "failed to register guest script module: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }

    PyObject *globals = PyModule_GetDict(pythonModule.get());
    PyObject *builtins = PyEval_GetBuiltins();
    if (PyObjectPtr fileName(PyUnicode_FromString(absolutePath.string().c_str()));
        globals == nullptr || builtins == nullptr || !fileName ||
        PyDict_SetItemString(globals, "__builtins__", builtins) < 0 ||
        PyDict_SetItemString(globals, "__file__", fileName.get()) < 0) {
        std::cerr << "failed to initialize guest script globals: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }

    PyObjectPtr code(
        Py_CompileString(source.c_str(), absolutePath.string().c_str(), Py_file_input));
    if (!code) {
        std::cerr << "failed to compile guest script: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }

    if (PyObjectPtr evaluation(PyEval_EvalCode(code.get(), globals, globals)); !evaluation) {
        std::cerr << "failed to load guest script: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }

    PyObjectPtr process(PyObject_GetAttrString(pythonModule.get(), "process"));
    if (!process || !PyCallable_Check(process.get())) {
        PyErr_Clear();
        PyErr_Format(
            PyExc_TypeError, "%s must define callable process(env)", absolutePath.string().c_str());
        std::cerr << "invalid guest script contract: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }

    PyObjectPtr result(PyObject_CallOneArg(process.get(), envelope));
    if (!result) {
        std::cerr << "guest script failed: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }
    if (result.get() != Py_None) {
        PyErr_Format(
            PyExc_TypeError, "%s process(env) must return None", absolutePath.string().c_str());
        std::cerr << "invalid guest script return value: " << absolutePath << '\n';
        PyErr_Print();
        return false;
    }
    return true;
}

bool writePacket(const std::filesystem::path &path,
                 const perception::container::envelope &envelope) {
    const auto packet = envelope.serialize();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "failed to open output packet: " << path << '\n';
        return false;
    }

    output.write(reinterpret_cast<const char *>(packet.data()),
                 static_cast<std::streamsize>(packet.size()));
    if (!output) {
        std::cerr << "failed to write output packet: " << path << '\n';
        return false;
    }
    return true;
}

int run(const Options &options) {
    PythonRuntime runtime;
    if (!configurePythonPaths(options)) {
        return EXIT_PYTHON_SETUP;
    }

    perception::container::envelope frameResults;
    {
        perception::python_bridge::scoped_envelope live(frameResults);
        for (size_t index = 0; index < options.scripts.size(); ++index) {
            if (!executeScript(options.scripts[index], index, live.py_object())) {
                return EXIT_SCRIPT;
            }
        }
    }

    return writePacket(options.output, frameResults) ? 0 : EXIT_OUTPUT;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        printUsage(std::cout, argv[0]);
        return 0;
    }

    Options options;
    if (!parseOptions(std::span(argv, static_cast<size_t>(argc)), options)) {
        printUsage(std::cerr, argv[0]);
        return EXIT_USAGE;
    }

    try {
        return run(options);
    } catch (const std::filesystem::filesystem_error &error) {
        std::cerr << error.what() << '\n';
        return EXIT_USAGE;
    } catch (const std::runtime_error &error) {
        std::cerr << error.what() << '\n';
        return Py_IsInitialized() ? EXIT_SCRIPT : EXIT_PYTHON_SETUP;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_OUTPUT;
    }
}
