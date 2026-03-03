# AMP Development Forge How-To start

## Host side dependencies
### Windows
   * [WSL](https://learn.microsoft.com/en-us/windows/wsl/install)
   * [Git](https://git-scm.com/install/)
   * [Docker (Desktop)](https://www.docker.com/products/docker-desktop/)
   * [Visual Studio Code](https://code.visualstudio.com/download)
   * **VSCode Dev Containers extension**
   * **WSL USB Manager 5.7.0** (Windows WSL)

### Linux
   * **Git**
   * **Docker**
   * **Visual Studio Code**
   * **VSCode Dev Containers extension**
   * **video4l2**

```bash
sudo apt-get update
sudo apt-get install -y git docker.io code v4l-utils
```

### Mac
   * **Git**
   * **Docker (Desktop)**
   * **Visual Studio Code**
   * **VSCode Dev Containers extension**

### Raspberry target
   * **Docker**
   * **Hailo packages**
   * **video4l2**
   * **raspicam**
   * For further details on Raspberry PI5 host installations please check out the relevant page: [How-To RPI5](how-to-rpi.md)

```bash
sudo apt-get update
sudo apt-get install -y git docker.io v4l-utils libraspberrypi-bin
```


Before starting the DevContainer, ensure that the `ssh-agent` is running and that your GitHub private key has been added to it.
This can be done in several ways depending on your operating system. The setup for Linux and macOS is as follows:
```bash
# For bash users: add ssh-agent to your .profile
eval "$(ssh-agent -s)"
```

The above command starts the SSH agent, which automatically adds your default keys (for example, `.ssh/id_rsa`, `.ssh/id_dsa`, etc.) from the `.ssh` directory.

If you need to add a different key, use the following command:

```bash
ssh-add [private_key_filename]
```

For further information and a detailed tutorial check out the following tutorial: [Generating a new SSH key and adding it to the ssh-agent](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent)

## Start the project
### Clone the repository
Either on your host or in case of Raspberry PI5 development open the repository with [Remote development extension](https://code.visualstudio.com/docs/remote/ssh).
For this to work you must be on the same local network as your raspberry device
   ```bash
   git clone git@github.com:Arm-Debug/amp-dev-forge.git
   cd amp-dev-forge
   ```

### Open AMP with VSCode
* Open command palette Ctrl+P(Windows/Linux) or Command+P(MAC) 
* Then "Reopen in Container"
* At this point every dependency, pre commit hook, and device should be ready to use inside the devcontainer.

### Build AMP
- **00 Build Project**: Builds all elements (default).
- **01 Clean Project**: Cleans build artifacts.
- **02 Build Tests**: Builds with tests enabled.
- **03 Run Tests**: Runs all tests.

### Start AMP
- Run the menu:
   ```bash
   ./scripts/amp-menu
   ```
- Options:
   - `-l` : start last pipeline
   - Alternatively the user can select a specific pipeline define under "scripts/pipelines"

### Debug AMP
- Use the "AMP Debug" configuration in VSCode (F5).

## Scripts and applications in our repository
Important scripts for usage:
- `amp-menu`: Main launcher for pipelines and demos.
- `build-elements.sh`: Build all GStreamer elements.
- `dev_init.sh`: Initialize dev environment and generate device YAMLs.
- `gen_audio.sh`, `gen_cam.sh`, `gen_npu.sh`, `gen_shared_memory.sh`: Generate docker-compose overrides for audio, camera, NPU, and shared memory.
- `docker-nuke.sh`: Stop and remove all Docker containers.
- `deployment-process.sh`: Steps for deployment.
- `serve-docs.sh`: Serve documentation locally.
- `run-ampperformance.sh`: Run performance overlay demo.
- `run-console`: Start a console in the devcontainer.
- `gen-doc.sh`: Generate documentation.

## Published Endpoints

- [Raspberry AMP Web UI](raspberrypi.local:9999)
- [Raspberry AMP Documentation](raspberrypi.local:8080)
- [PC AMP Web UI](localhost:9999)
- [PC AMP Documentation](localhost:8080)

- **Hostnames:**
   - `raspberrypi.local` (on Raspberry Pi)
   - `localhost` (on your development machine)
- **Ports:**
   - `9999` (AMP Web UI)
   - `8080` (Documentation)
   - Other ports may be used by ampsink or for streaming endpoints.

##  Quality checks
   expkits-ci is a tool that is automatically installed during the creation of the container.
   Most quality checks, both in CI and locally, are performed by this tool.

   For help on the `container side`, run: `expkits-ci --help`
   ```bash
   amp-dev-forge $ expkits-ci --help
   usage: __main__.py [-h] [-bn] [-cm] [-jt] [-clfc] [-clf] [-clt] [-pyfc] [-pyf] [-cmfc] [-cmf] [-shfc] [-shf] [-lhc] [-lh] [-v] [-ac] [-do] [-pr PR_TARGET_BRANCH] [-lo {stdout,file,both}] [-lf LOG_FILE]
                     [-lof LIST_OF_FILES [LIST_OF_FILES ...]]

   ...

   (.venv-ci) ubuntu@387b974701cb:/workspaces/amp-dev-forge$
   ```

   To check your changes, a set of plugins are already set up in the environment, but you can alternatively:
- Call expkits-ci directly.
- Run the following task: 9 - Run CI checks for current file.
- Run the following task: 9 - Run full CI checks.
- Run the installed pre-commit hooks manually or with a commit.

   ```bash
   git pre-commit run
   ```

- To run without pre commit hooks simply:

   ```bash
   git commit --no-verify
   ```
