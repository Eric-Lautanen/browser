const { JSDOM } = require('jsdom');
const fs = require('fs');

function serializeNode(node) {
    if (!node) return null;
    if (node.nodeType === 1) { // ELEMENT_NODE
        const attrs = {};
        if (node.attributes) {
            for (const attr of node.attributes) {
                attrs[attr.name] = attr.value;
            }
        }
        const children = [];
        for (const child of node.childNodes) {
            const s = serializeNode(child);
            if (s) children.push(s);
        }
        return {
            type: 'element',
            tag: node.tagName.toLowerCase(),
            attributes: attrs,
            children: children
        };
    } else if (node.nodeType === 3) { // TEXT_NODE
        const data = node.nodeValue;
        const normalized = data.replace(/[\t\n\r]+/g, ' ').replace(/ +/g, ' ').trim();
        return {
            type: 'text',
            data: data,
            data_normalized: normalized
        };
    } else if (node.nodeType === 8) { // COMMENT_NODE
        return {
            type: 'comment',
            data: node.nodeValue
        };
    } else if (node.nodeType === 10) { // DOCUMENT_TYPE_NODE
        return {
            type: 'doctype',
            name: node.name,
            public_id: node.publicId,
            system_id: node.systemId
        };
    }
    return null;
}

function getDeclaredEncoding(doc) {
    const meta = doc.querySelector('meta[charset]');
    if (meta) return meta.getAttribute('charset');
    const meta2 = doc.querySelector('meta[http-equiv="Content-Type"]');
    if (meta2) {
        const ct = meta2.getAttribute('content');
        if (ct) {
            const m = ct.match(/charset=([^;]+)/i);
            if (m) return m[1].trim();
        }
    }
    return null;
}

function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) {
        console.error('Usage: node reference_html.js <file.html>');
        process.exit(1);
    }
    const filePath = args[0];
    const html = fs.readFileSync(filePath, 'utf-8');
    const dom = new JSDOM(html, {
        includeNodeLocations: false,
        contentType: 'text/html'
    });

    const doc = dom.window.document;
    const output = {
        source: filePath,
        encoding: getDeclaredEncoding(doc),
        doctype: doc.doctype ? {
            name: doc.doctype.name,
            public_id: doc.doctype.publicId,
            system_id: doc.doctype.systemId
        } : null,
        children: [],
        quirks_mode: doc.compatMode === 'BackCompat'
    };

    for (const child of doc.childNodes) {
        const s = serializeNode(child);
        if (s) output.children.push(s);
    }

    console.log(JSON.stringify(output, null, 2));
}

main();
