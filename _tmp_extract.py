import fitz

specs = [('THREE PROBES_CONTROL_PHYDP8384_V3.pdf', 'v3_text.txt'),
         ('yuanlitu1.pdf', 'old_text.txt')]
for src, out in specs:
    d = fitz.open(src)
    with open(out, 'w', encoding='utf-8', errors='replace') as f:
        for i in range(d.page_count):
            f.write('\n===== PAGE %d =====\n' % (i + 1))
            f.write(d[i].get_text())
    print(src, '->', out, 'pages=', d.page_count)