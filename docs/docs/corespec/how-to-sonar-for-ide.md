# SonarQube for IDE setup in VS Code

This note describes how to set up **SonarQube for IDE** in **VS Code** in connected mode for a C/C++ project using `compile_commands.json`.

> Note: The SonarQube for IDE extension is installed automatically via the devcontainer.

---

## 1. Generate a SonarQube user token

Connected mode requires a **user token**.

To generate it:

1. Log in to SonarQube
2. Go to **User → My Account → Security**
3. Create a new token
4. Select type: **User**
5. Copy the token and store it securely

⚠️ Do not use:
- project tokens
- global tokens

---

## 2. Generate `compile_commands.json`

For C/C++, SonarQube for IDE relies on the compilation database.

This is done automatically by `scripts/build-elements.sh`.

## 3. Configure VS Code user settings

Open User Settings (JSON) and add:

```json
{

  "sonarlint.connectedMode.connections.sonarqube": [
    {
      "serverUrl": "https://sonarqube.mobilestudio.aws.arm.com",
      "connectionId": "ArmSonar",
      "token": "YOUR_USER_TOKEN_HERE"
    }
  ],
  "sonarlint.output.showVerboseLogs": true
}

```
## 4. Configure workspace settings

In .vscode/settings.json:
```json
{
  "sonarlint.pathToCompileCommands": "/work/development/build/compile_commands.json",
  "sonarlint.connectedMode.project": {
    "connectionId": "ArmSonar",
    "projectKey": "LinuxVisionKitAmp_3PIaCH2mDmayKH"
  }
}
```
**Notes:**
* Use an absolute path for compile_commands.json
* connectionId must match the user settings
* projectKey must be the SonarQube project key (not the display name)

## 5. Verify setup

Open the SonarQube for IDE Output panel in VS Code and check:

* connection is successful
* project is bound
* branch is detected
* compile commands are loaded

## Troubleshooting
### 403 Insufficient privileges

If you see:

```
403 Insufficient privileges
```

Then:
* the token is valid
* but the user does not have enough permissions on the project

## Summary

You need:

* a user token
* a valid compile_commands.json
* correct user + workspace settings

Everything else is handled automatically by the devcontainer.

