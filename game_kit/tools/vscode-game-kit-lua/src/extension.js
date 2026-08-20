const vscode = require("vscode");
const path = require("path");

function activate(context) {
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("game-kit-lua", {
      createDebugAdapterDescriptor() {
        return new vscode.DebugAdapterExecutable(process.execPath, [
          path.join(__dirname, "adapter.js"),
        ]);
      },
    })
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
