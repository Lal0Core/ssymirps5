#!/usr/bin/env python3
"""Build SSYmirPS5 icon/DDS assets from its editable PNG artwork.

Usage: make_ssymirps5_art.py SCE_SYS_DIR [--icon PNG --background PNG]
Regeneration reuses the selected current design instead of silently restoring older artwork.
"""
from pathlib import Path
import argparse,sys
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bc7

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out',type=Path)
    parser.add_argument('--icon',type=Path)
    parser.add_argument('--background',type=Path)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    icon=Image.open(args.icon or args.out/'icon-source.png').convert('RGB')
    background=Image.open(args.background or args.out/'background-source.png').convert('RGB')
    if abs(icon.width/icon.height-1)>0.01: raise SystemExit('Icon must be square')
    if abs(background.width/background.height-16/9)>0.01: raise SystemExit('Background must be 16:9')
    icon.save(args.out/'icon-source.png')
    icon.resize((512,512),Image.Resampling.LANCZOS).save(args.out/'icon0.png',optimize=True)
    background=background.resize((3840,2160),Image.Resampling.LANCZOS)
    background.save(args.out/'background-source.png',optimize=True)
    rgb=np.asarray(background,dtype=np.uint8)
    encoded=bc7.encode_bc7(rgb)
    decoded=bc7.decode_bc7_mode6(encoded,3840,2160)
    mse=np.mean((decoded.astype(np.float64)-rgb.astype(np.float64))**2)
    psnr=10*np.log10(255**2/max(mse,1e-9))
    if psnr<30: raise SystemExit('BC7 quality check failed: %.2f dB'%psnr)
    blob=bc7.dds_header(3840,2160,len(encoded))+encoded
    for name in ['pic0.dds','pic1.dds']: (args.out/name).write_bytes(blob)
    print('SSYmirPS5 art: 512px icon, 3840x2160 BC7 backgrounds, PSNR %.2f dB'%psnr)

if __name__=='__main__': main()
