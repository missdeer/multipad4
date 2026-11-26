from pathlib import Path
import fitz,csv,statistics
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('cmake-msvc-build/printing-separator-test')
for sample in range(3):
 for kind in ('all','range'):
  before=fitz.open(root/f'before-{kind}-{sample}.pdf');after=fitz.open(root/f'after-{kind}-{sample}.pdf')
  assert len(before)==len(after)
  for a,b in zip(before,after):
   assert a.get_text()==b.get_text()
   assert a.get_pixmap().samples==b.get_pixmap().samples
  assert len(after)==(2 if kind=='range' else 40),(kind,len(after))
  assert ('Line 50 printing body verification' if kind=='range' else 'Line 0 printing body verification') in after[0].get_text()
  for rect in (fitz.Rect(20,20,580,80),fitz.Rect(20,730,580,800)):
   pix=after[0].get_pixmap(clip=rect);assert min(pix.samples)<200
  print(sample,kind,'pages',len(after),'text+pixels identical')
print((root/'print-driver-pairs.csv').read_text())


