"""Explicit developer conversion of compiled P0 data to reviewed native XML.
Run from the repository root after exporting and reviewing the static baseline.
Never called by an ordinary product build. Upstream data: notepad4/License.txt.
"""
from pathlib import Path
import xml.etree.ElementTree as E,re,collections,configparser
b=E.parse('tests/baselines/lexer-catalog.xml').getroot();new=E.Element('languages',version='1',upstream=b.get('upstream'),license=b.get('license'))
nums={k:int(v) for k,v in re.findall(r'^#define\s+(SCE_\w+)\s+(\d+)\b',Path('3rdparty/notepad4-scintilla/scintilla/include/SciLexer.h').read_text(),re.M)}
sci=Path('3rdparty/notepad4-scintilla/scintilla/include/SciLexer.h').read_text()
expressions=dict(re.findall(r'^#define\s+(SCE_\w+)\s+([^\n]+)',sci,re.M))
for repeat in range(30):
 for key,value in expressions.items():
  if key in nums:continue
  value=value.split('//')[0].strip()
  for func,off in [('php_style','SCE_STYLE_OFFSET_PHP'),('js_style','SCE_STYLE_OFFSET_JS'),('css_style','SCE_STYLE_OFFSET_CSS')]:
   value=re.sub(r'\b'+func+r'\(([^)]+)\)',r'(\1 + '+off+')',value)
  value=re.sub(r'\bSCE_\w+\b',lambda m:str(nums[m[0]]) if m[0] in nums else m[0],value)
  if re.fullmatch(r'[0-9xXa-fA-F ()+|<>-]+',value):
   try:nums[key]=int(eval(value,{'__builtins__':{}},{}))
   except (ValueError,SyntaxError):pass
bindings=[]
for old in b.findall('language'):
 vals={x.get('name'):x.get('value') for x in old.findall('field')};l=E.SubElement(new,'language',id=old.get('id'),legacyIndex=old.get('legacyIndex'),source=old.get('source'))
 for el in old:
  if el.tag not in ('runtimeExtensions','runtimeStyleBuffer'):l.append(E.fromstring(E.tostring(el)))
 source=Path(old.get('source')).read_text(encoding='utf-8-sig');tokens=re.findall(r'\bSCE_\w+',source[source.find('static const EDITSTYLE'):]);prefixes=collections.Counter('_'.join(t.split('_')[:2])+'_' for t in tokens);prefix=prefixes.most_common(1)[0][0] if prefixes else ''
 cat=E.SubElement(l,'categories');used=set()
 for style in l.findall('styles/style'):
  packed=int(style.get('packed'));group=style.get('id')
  if old.get('id') in ['global','ansi']:continue
  native=[packed&255];rest=packed>>8
  while rest:native.append(rest&255);rest>>=8
  for n in native:
   names=[k[len(prefix):].lower() for k,v in nums.items() if v==n and k.startswith(prefix)]
   suffix=names[0] if names else next((k[4:].lower() for k,v in nums.items() if v==n and k in tokens),'default')
   suffix={'comment':'block','commentline':'line','commentdoc':'documentation','commentlinedoc':'documentation.line','character':'character','string':'quoted','stringeol':'unterminated','word':'primary','2ndword':'secondary','macro':'definition','macro2':'reference'}.get(suffix,suffix)
   cid=group if len(native)==1 else group+'.'+suffix
   if cid in used:raise ValueError((cid,n,prefix,names))
   used.add(cid);E.SubElement(cat,'category',id=cid,group=group,native=str(n))
 bindings.append((old.get('id'),vals['iLexer'],vals['rid']))
for e in b.findall('dialectCompletion'):new.append(E.fromstring(E.tostring(e)))
reviewed=E.parse('config/languages/default.xml').getroot()
for language in new.findall('language'):
 previous=next(node for node in reviewed.findall('language') if node.get('id')==language.get('id'))
 existing={node.get('id') for node in language.findall('categories/category')}
 for category in previous.findall('categories/category'):
  if category.get('id') not in existing:language.find('categories').append(E.fromstring(E.tostring(category)))
 for name in ['variants','aliases']:
  language.append(E.fromstring(E.tostring(previous.find(name))))
E.indent(new,space='  ');E.ElementTree(new).write('config/languages/default.xml',encoding='utf-8',xml_declaration=True)
Path('src/LexerBindings.h').write_text('#pragma once\nstruct LexerBinding { const char* id; int engine, resource; };\ninline constexpr LexerBinding lexerBindings[] = {\n'+''.join('    {"%s", %s, %s},\n'%x for x in bindings)+'};\n',encoding='utf-8')
p=Path('3rdparty/notepad4/doc/Notepad4 DarkTheme.ini');raw=p.read_bytes();ini=configparser.ConfigParser(interpolation=None,strict=False);ini.optionxform=str;ini.read_string(raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig'))
for dark in [False,True]:
 t=E.Element('theme',version='1',id='dark' if dark else 'light',name='MultiPad4 Dark' if dark else 'MultiPad4 Light');E.SubElement(t,'style',id='editor.default',fg='DCDCDC' if dark else '20242A',bg='1E1E1E' if dark else 'FFFFFF')
 if dark:
  for l in new.findall('language'):
   for s in l.findall('styles/style'):
    if ini.has_option(l.findtext('name'),s.get('name')):E.SubElement(t,'style',id=s.get('id'),legacy=ini.get(l.findtext('name'),s.get('name')))
 E.indent(t,space='  ');E.ElementTree(t).write('config/themes/'+('dark' if dark else 'light')+'.xml',encoding='utf-8',xml_declaration=True)
