const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

function sortAttrs(obj) {
    if (obj && typeof obj === 'object') {
        if (Array.isArray(obj)) {
            obj.forEach(sortAttrs);
        } else if (obj.attributes && typeof obj.attributes === 'object' && !Array.isArray(obj.attributes)) {
            const sorted = {};
            Object.keys(obj.attributes).sort().forEach(k => { sorted[k] = obj.attributes[k]; });
            obj.attributes = sorted;
        }
        for (const v of Object.values(obj)) {
            sortAttrs(v);
        }
    }
    return obj;
}

const testsDir = path.resolve(__dirname, 'tests');
const files = process.argv.slice(2);

if (files.length === 0) {
    console.error('Usage: node regenerate_expected.js file1.html file2.html ...');
    process.exit(1);
}

for (const file of files) {
    const filePath = path.resolve(testsDir, file);
    if (!fs.existsSync(filePath)) {
        console.error(`File not found: ${filePath}`);
        continue;
    }
    
    const output = execSync(`node "${path.join(__dirname, 'reference_html.js')}" "${filePath}"`, { encoding: 'utf-8' });
    let ref = JSON.parse(output);
    
    // Sort attributes alphabetically (matching engine behavior)
    sortAttrs(ref);
    
    // Normalize source to basename
    ref.source = path.basename(file);
    
    // Set encoding to UTF-8 (engine default)
    if (ref.encoding === null) {
        ref.encoding = 'UTF-8';
    }
    
    const expectedPath = filePath.replace(/\.html$/, '.expected-dom.json');
    fs.writeFileSync(expectedPath, JSON.stringify(ref, null, 2) + '\n');
    console.log(`Regenerated: ${expectedPath}`);
}
