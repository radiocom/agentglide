// Read-only compatibility probe: a canonical path must preserve the virtual view.
// Run with cwd=Base, once directly and once through agentglide-run.
const fs = require('node:fs');
const path = require('node:path');
const cwd = process.cwd();
const canonical = fs.realpathSync.native(cwd);
function read(root) {
  try {
    return { value: fs.readFileSync(path.join(root, 'fallback.txt'), 'utf8').trim() };
  } catch (error) {
    return { error: error.code };
  }
}
console.log(JSON.stringify({ cwd, canonical, cwdRead: read(cwd), canonicalRead: read(canonical) }, null, 2));
