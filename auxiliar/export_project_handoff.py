#!/usr/bin/env python3
"""Export a UTF-8 handoff, readable PDF and portable current source snapshot.

No firmware writes, hardware access, Git commits or network actions are made.
ReportLab and Arial/Consolas fonts are required for PDF generation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import textwrap
import zipfile
from xml.sax.saxutils import escape


def make_pdf(markdown: str, output: Path, fonts: Path) -> int:
    from reportlab.lib import colors
    from reportlab.lib.enums import TA_LEFT
    from reportlab.lib.pagesizes import A4
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    from reportlab.platypus import (
        Flowable, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle,
    )

    pdfmetrics.registerFont(TTFont('HandoffBody', str(fonts / 'arial.ttf')))
    pdfmetrics.registerFont(TTFont('HandoffBold', str(fonts / 'arialbd.ttf')))
    pdfmetrics.registerFont(TTFont('HandoffCode', str(fonts / 'consola.ttf')))
    pdfmetrics.registerFontFamily('HandoffBody', normal='HandoffBody', bold='HandoffBold')
    body = ParagraphStyle('Body', fontName='HandoffBody', fontSize=9.5,
                          leading=13.5, spaceAfter=7, alignment=TA_LEFT,
                          splitLongWords=True)
    title = ParagraphStyle('Title', parent=body, fontName='HandoffBold',
                           fontSize=19, leading=23, spaceAfter=14)
    heading = ParagraphStyle('Heading', parent=body, fontName='HandoffBold',
                             fontSize=13, leading=17, spaceBefore=13,
                             spaceAfter=8, keepWithNext=True,
                             textColor=colors.HexColor('#23455f'))
    subheading = ParagraphStyle('Subheading', parent=heading, fontSize=10.5,
                                leading=14, spaceBefore=7)
    cell = ParagraphStyle('Cell', parent=body, fontSize=8, leading=11,
                          spaceAfter=0)
    bullet = ParagraphStyle('Bullet', parent=body, leftIndent=12,
                            firstLineIndent=-9)
    width = A4[0] - 84
    page_counts = []

    def inline(s: str) -> str:
        s = escape(s)
        s = re.sub(r'`([^`]+)`', r'<font name="HandoffCode">\1</font>', s)
        s = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', s)
        return s

    class CodeLine(Flowable):
        """One bounded line per flowable permits page breaks within long blocks."""
        def __init__(self, value):
            super().__init__()
            self.value = value
            self.width = width
            self.height = 12

        def draw(self):
            self.canv.setFillColor(colors.HexColor('#f0f3f6'))
            self.canv.rect(0, 0, width, 12, fill=1, stroke=0)
            self.canv.setFillColor(colors.HexColor('#263746'))
            self.canv.setFont('HandoffCode', 7.5)
            self.canv.drawString(6, 3, self.value)

    story = []
    lines = markdown.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith('```'):
            story.append(Spacer(1, 4))
            i += 1
            while i < len(lines) and not lines[i].startswith('```'):
                # Explicit display wraps preserve all source characters in PDF.
                for segment in textwrap.wrap(lines[i].expandtabs(4), width=108,
                                             break_long_words=True,
                                             break_on_hyphens=False,
                                             replace_whitespace=False,
                                             drop_whitespace=False) or ['']:
                    story.append(CodeLine(segment))
                i += 1
            story.append(Spacer(1, 7))
            i += 1
            continue
        if line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                values = [v.strip() for v in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch(r':?-+:?', v) for v in values):
                    rows.append(values)
                i += 1
            count = len(rows[0])
            factors = [1 / count] * count
            if count == 2:
                factors = [0.43, 0.57]
            elif count == 4:
                factors = [0.14, 0.22, 0.22, 0.42]
            data = [[Paragraph(inline(v), cell) for v in row] for row in rows]
            table = Table(data, colWidths=[width * f for f in factors],
                          repeatRows=1, hAlign='LEFT')
            table.setStyle(TableStyle([
                ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#e3ebf2')),
                ('GRID', (0, 0), (-1, -1), 0.35, colors.HexColor('#bbc8d4')),
                ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                ('LEFTPADDING', (0, 0), (-1, -1), 6),
                ('RIGHTPADDING', (0, 0), (-1, -1), 6),
                ('TOPPADDING', (0, 0), (-1, -1), 5),
                ('BOTTOMPADDING', (0, 0), (-1, -1), 5),
            ]))
            story.append(table)
            story.append(Spacer(1, 8))
            continue
        if line.startswith('# '):
            story.append(Paragraph(inline(line[2:]), title))
        elif line.startswith('## '):
            story.append(Paragraph(inline(line[3:]), heading))
        elif line.startswith('### '):
            story.append(Paragraph(inline(line[4:]), subheading))
        elif line.startswith('- '):
            story.append(Paragraph('• ' + inline(line[2:]), bullet))
        elif re.match(r'^\d+\. ', line):
            story.append(Paragraph(inline(line), bullet))
        else:
            paragraph = [line]
            i += 1
            while i < len(lines) and lines[i].strip() and not re.match(
                    r'^(#|\||```|- |\d+\. )', lines[i]):
                paragraph.append(lines[i])
                i += 1
            story.append(Paragraph(inline(' '.join(paragraph)), body))
            continue
        i += 1

    def footer(canvas, document):
        canvas.saveState()
        canvas.setFont('HandoffBody', 8)
        canvas.setFillColor(colors.HexColor('#526572'))
        canvas.drawString(42, 25, 'I4A · Continuidad del proyecto · 11/10/2026')
        canvas.drawRightString(A4[0] - 42, 25, f'Página {document.page}')
        canvas.restoreState()
        page_counts.append(document.page)

    document = SimpleDocTemplate(str(output), pagesize=A4, leftMargin=42,
                                 rightMargin=42, topMargin=38, bottomMargin=43,
                                 title='Continuidad del proyecto I4A — 11/10/2026',
                                 author='Proyecto I4A', pageCompression=1)
    document.build(story, onFirstPage=footer, onLaterPages=footer)
    data = output.read_bytes()
    if not data.startswith(b'%PDF-') or not data.rstrip().endswith(b'%%EOF'):
        raise RuntimeError('Invalid PDF output')
    if b'/ToUnicode' not in data:
        raise RuntimeError('Expected embedded Unicode font mapping')
    return max(page_counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--fonts-dir', type=Path, default=Path('C:/Windows/Fonts'))
    parser.add_argument('--replace-output', action='store_true',
                        help='Replace only the three named handoff outputs')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    base = 'Continuidad_proyecto_I4A_2026-10-11'
    md_path = output / (base + '.md')
    pdf_path = output / (base + '.pdf')
    zip_path = output / (base + '.zip')
    for target in (md_path, pdf_path, zip_path):
        if target.exists() and not args.replace_output:
            raise FileExistsError(f'Choose a new output directory: {target}')
    source_doc = root / 'docs/continuidad-proyecto-20261011.md'
    markdown = source_doc.read_text(encoding='utf-8')
    if '\ufffd' in markdown:
        raise RuntimeError('Document contains a Unicode replacement character')
    md_path.write_bytes(source_doc.read_bytes())
    pages = make_pdf(markdown, pdf_path, args.fonts_dir)

    entries = {}
    def add_file(path: Path, name: str):
        if path.is_file():
            entries[name] = path.read_bytes()

    names = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
        cwd=root).decode('utf-8').split('\0')
    allowed_roots = {'app', 'auxiliar', 'components', 'docker', 'docs', 'gateway', 'tests'}
    skipped_suffixes = {'.log', '.pyc', '.pyo', '.exe', '.dll', '.o', '.obj'}
    for name in sorted(set(names)):
        if not name:
            continue
        path = Path(name)
        if len(path.parts) > 1 and path.parts[0] not in allowed_roots:
            continue
        if any(part in {'build', '.git', '.venv', 'venv', '__pycache__'} for part in path.parts):
            continue
        if path.suffix.lower() in skipped_suffixes:
            continue
        if path.name in {'.env', 'sdkconfig.old'}:
            continue
        add_file(root / path, 'proyecto/' + path.as_posix())
    add_file(root / 'app/sdkconfig', 'proyecto/app/sdkconfig')

    for bundle in ('firmware_bundle_coord_v2_ramona_20261010',
                   'firmware_bundle_coord_v2_grillo_root_20261010'):
        folder = root / bundle
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                add_file(path, 'firmware/' + bundle + '/' + path.relative_to(folder).as_posix())

    evidence = [
        'pc-ping-baseline-20261010.csv', 'pc-failover-20261010.csv',
        'pc-test-north-20261010.log', 'pc-test-cut-20261010.log',
        'pc-route-review-baseline-20261010.csv', 'pc-monitor-crosscheck-20261010.csv',
        'coord-v2-baseline-20261011.csv', 'coord-v2-east-before-cut-20261011.log',
        'root-session-loss-20261011.log', 'ramona-east-session-loss-20261011.log',
        'ramona-north-spi-20261011.log',
        'coord-v2-ramona-ota.log', 'coord-v2-grillo-ota-retry.log', 'coord-v2-root-ota.log',
        'routing-token-failure-proof.c', 'routing_lost_request_proof.c',
        'routing_lost_request_proof.py',
    ]
    missing = []
    for name in evidence:
        path = root / 'build/firmware' / name
        if path.is_file():
            add_file(path, 'evidencia/' + name)
        else:
            missing.append(name)
    add_file(root / 'docs/evidencia/telemetria-recibida-20261011.json',
             'evidencia/telemetria-recibida-20261011.json')

    validation = output / 'i4a_validacion_coord_v2_20261011_v2.zip'
    add_file(validation, 'validacion/' + validation.name)
    add_file(md_path, md_path.name)
    add_file(pdf_path, pdf_path.name)
    status = subprocess.check_output(['git', 'status', '--short'], cwd=root).decode('utf-8')
    entries['estado-git-original.txt'] = status.encode('utf-8')
    entries['cambios-rastreados.patch'] = subprocess.check_output(
        ['git', 'diff', '--binary', 'HEAD', '--'], cwd=root, stderr=subprocess.DEVNULL)
    entries['LEER_PRIMERO.txt'] = (
        'Continuidad I4A — 11/10/2026\n\n'
        f'Leer {base}.md o su PDF.\n'
        'proyecto/: árbol actual, sin .git ni compilación genérica. Incluye app/sdkconfig.\n'
        'firmware/: bundles verificados instalados; anteriores al modo rápido.\n'
        'evidencia/: selección de registros anteriores y extracto recibido de la RPi.\n'
        'Los logs pueden tener bytes o líneas dañadas; no son prueba aislada de hardware muerto.\n'
        'El patch cubre archivos rastreados; los nuevos están en proyecto/.\n'
        'Nada en este paquete ordena flashear, reiniciar ni modificar red automáticamente.\n'
        'El documento no reproduce las contraseñas del chat ni claves privadas.\n'
        'El código conserva las credenciales predeterminadas que forman parte del proyecto;\n'
        'compartir el paquete con colaboradores, no publicarlo sin revisión.\n'
    ).encode('utf-8')
    manifest = {
        'date': '2026-10-11', 'branch': 'feat/redundant-link-discovery',
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root).decode().strip(),
        'pdf_pages': pages, 'missing_optional_evidence': missing,
        'entries': [{'path': name, 'bytes': len(data),
                     'sha256': hashlib.sha256(data).hexdigest()}
                    for name, data in sorted(entries.items())],
    }
    entries['manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(entries.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(zip_path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('ZIP integrity failed')
        for item in manifest['entries']:
            data = archive.read(item['path'])
            if hashlib.sha256(data).hexdigest() != item['sha256']:
                raise RuntimeError(f'Bad manifest digest: {item["path"]}')
    print(json.dumps({'markdown': str(md_path), 'pdf': str(pdf_path),
                      'pdf_pages': pages, 'zip': str(zip_path),
                      'zip_bytes': zip_path.stat().st_size,
                      'files': len(entries), 'missing_evidence': missing}, ensure_ascii=False))


if __name__ == '__main__':
    main()
