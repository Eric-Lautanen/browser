const postcss = require('postcss');
const fs = require('fs');

function serializeDeclaration(decl) {
    return {
        property: decl.prop,
        value: decl.value,
        important: decl.important,
        source_index: decl.source && decl.source.start ? decl.source.start.line : 0
    };
}

function serializeRule(rule, index) {
    const selectors = rule.selectors || [];
    const declarations = [];
    for (const decl of rule.nodes || []) {
        if (decl.type === 'decl') {
            declarations.push(serializeDeclaration(decl));
        }
    }
    return {
        type: 'rule',
        selectors: selectors,
        declarations: declarations,
        source_index: index
    };
}

function serializeAtRule(atRule) {
    const result = {
        type: 'at-rule',
        name: atRule.name,
        params: atRule.params || '',
        rules: [],
        at_rules: [],
        declarations: []
    };

    if (atRule.nodes) {
        let ruleIdx = 0;
        for (const node of atRule.nodes) {
            if (node.type === 'rule') {
                result.rules.push(serializeRule(node, ruleIdx++));
            } else if (node.type === 'atrule') {
                result.at_rules.push(serializeAtRule(node));
            } else if (node.type === 'decl') {
                result.declarations.push(serializeDeclaration(node));
            }
        }
    }

    // Handle @keyframes specially
    if (atRule.name === 'keyframes') {
        result.keyframes = {
            name: atRule.params,
            blocks: []
        };
        if (atRule.nodes) {
            for (const node of atRule.nodes) {
                if (node.type === 'rule') {
                    const positions = (node.selectors || []).map(s => {
                        if (s === 'from') return 0;
                        if (s === 'to') return 100;
                        return parseFloat(s);
                    });
                    const decls = [];
                    for (const decl of node.nodes || []) {
                        if (decl.type === 'decl') {
                            decls.push(serializeDeclaration(decl));
                        }
                    }
                    result.keyframes.blocks.push({
                        positions: positions,
                        declarations: decls
                    });
                }
            }
        }
    }

    return result;
}

function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) {
        console.error('Usage: node reference_css.js <file.css>');
        process.exit(1);
    }
    const filePath = args[0];
    const css = fs.readFileSync(filePath, 'utf-8');

    const root = postcss.parse(css, { from: filePath });

    const output = {
        source: filePath,
        rules: [],
        at_rules: []
    };

    let ruleIdx = 0;
    for (const node of root.nodes) {
        if (node.type === 'rule') {
            output.rules.push(serializeRule(node, ruleIdx++));
        } else if (node.type === 'atrule') {
            output.at_rules.push(serializeAtRule(node));
        }
    }

    console.log(JSON.stringify(output, null, 2));
}

main();
