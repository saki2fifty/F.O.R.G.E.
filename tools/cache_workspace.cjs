// Scoped to cache actions via NODE_OPTIONS. GitHub's default checkout path ends
// in a period for this repository and cannot be opened by Windows GNU tar.
const fs = require('node:fs');
const path = require('node:path');
const workspace = path.join(process.env.RUNNER_TEMP, 'forge-work', 'FORGE');
if (!fs.statSync(workspace).isDirectory()) {
    throw new Error('The short FORGE checkout must exist before caching');
}
process.env.GITHUB_WORKSPACE = workspace;
process.chdir(workspace);
