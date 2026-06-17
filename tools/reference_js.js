const acorn = require('acorn');
const fs = require('fs');

function parseJS(code, sourceType) {
    try {
        const ast = acorn.parse(code, {
            ecmaVersion: 2022,
            sourceType: sourceType || 'script',
            locations: true,
            ranges: false
        });
        return { success: true, ast: ast };
    } catch (err) {
        return {
            success: false,
            error: {
                message: err.message,
                line: err.loc ? err.loc.line : null,
                column: err.loc ? err.loc.column : null
            }
        };
    }
}

function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) {
        console.error('Usage: node reference_js.js <file.js>');
        process.exit(1);
    }
    const filePath = args[0];
    const code = fs.readFileSync(filePath, 'utf-8');

    // Try as script first, then as module
    let result = parseJS(code, 'script');
    let sourceType = 'script';
    if (!result.success && code.includes('export') || code.includes('import')) {
        result = parseJS(code, 'module');
        sourceType = 'module';
    }

    const output = {
        source: filePath,
        source_type: sourceType,
        result: result
    };

    console.log(JSON.stringify(output, null, 2));
}

main();
