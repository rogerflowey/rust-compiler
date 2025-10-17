import os
import re
from pathlib import Path

def is_code_like(url):
    # Skip if url contains patterns typical of code, not links
    if any(char in url for char in ['&', '(', ')', 'const ', 'auto']):
        return True
    if url.startswith('const ') or url.startswith('auto'):
        return True
    return False

def find_markdown_links(file_path):
    links = []
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Split content into parts, skipping code blocks
    parts = re.split(r'```', content)
    for i, part in enumerate(parts):
        if i % 2 == 0:  # even parts are not code blocks
            # Find links in this part
            lines = part.split('\n')
            for line_num, line in enumerate(lines, 1):
                matches = re.findall(r'\[([^\]]*)\]\(([^)]+)\)', line)
                for match in matches:
                    text, url = match
                    if not is_code_like(url):
                        links.append((line_num, text, url))
    return links

def resolve_path(base_dir, url):
    if url.startswith('http'):
        return None  # external link, skip
    if url.startswith('#'):
        return None  # anchor, skip for now
    # Strip line numbers or anchors
    url = url.split(':')[0]
    # Resolve relative path
    full_path_str = os.path.normpath(str(base_dir / url))
    full_path = Path(full_path_str)
    return full_path

def check_links(docs_dir):
    docs_path = Path(docs_dir)
    all_links = []
    for md_file in docs_path.rglob('*.md'):
        base_dir = md_file.parent
        links = find_markdown_links(md_file)
        for line_num, text, url in links:
            resolved = resolve_path(base_dir, url)
            if resolved:
                exists = resolved.exists()
                all_links.append({
                    'file': str(md_file),
                    'line': line_num,
                    'text': text,
                    'url': url,
                    'resolved': str(resolved),
                    'exists': exists
                })
    return all_links

if __name__ == '__main__':
    docs_dir = '/home/rogerw/project/compiler/docs'
    links = check_links(docs_dir)
    outdated = [link for link in links if not link['exists']]
    print(f"Total links checked: {len(links)}")
    print(f"Outdated links: {len(outdated)}")
    for link in outdated:
        print(f"{link['file']}:{link['line']}: [{link['text']}]({link['url']}) -> {link['resolved']} (does not exist)")