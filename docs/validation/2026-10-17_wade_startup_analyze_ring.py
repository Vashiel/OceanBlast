from pathlib import Path
import csv,json,hashlib,numpy as np
b=Path('build/2026-10-17-wade-startup');ring=list(csv.DictReader((b/'ring.ring.csv').open()));rows=list(csv.DictReader((b/'ring.csv').open()));raw=np.fromfile(b/'ring.pcm','<i2');t0=int(rows[2659]['guest_ticks'])/2e7
summary={'tracked_physical_byte_range':['0x30350000','0x303507ff'],'pcm_matches_uninstrumented':hashlib.sha256((b/'ring.pcm').read_bytes()).digest()==hashlib.sha256((b/'current.pcm').read_bytes()).digest(),'sdram_matches_uninstrumented':hashlib.sha256((b/'ring.sdram').read_bytes()).digest()==hashlib.sha256((b/'current.sdram').read_bytes()).digest(),'windows':[],'stale_music_callbacks':[]}
for lo,hi in [(0,2),(2,5),(5,10)]:
    ids=[i for i,r in enumerate(rows) if lo<=int(r['guest_ticks'])/2e7-t0<hi];v={k:sum(int(ring[i][k]) for i in ids) for k in ['stale_bytes','fresh_bytes','never_written_bytes']};v.update(music_guest_window=[lo,hi],callbacks=len(ids),total_callback_bytes=sum(int(rows[i]['samples'])*2 for i in ids));summary['windows'].append(v)
for i in range(2659,len(rows)):
    if not int(ring[i]['stale_bytes']):continue
    r=rows[i];p=raw[int(r['sample_offset']):int(r['sample_offset'])+int(r['samples'])];matches=[]
    for j in range(max(2659,i-16),i):
        q=rows[j]
        if q['samples']==r['samples'] and np.array_equal(p,raw[int(q['sample_offset']):int(q['sample_offset'])+len(p)]):matches.append(j)
    summary['stale_music_callbacks'].append(dict(callback=i,guest_seconds_after_music=int(r['guest_ticks'])/2e7-t0,stale_bytes=int(ring[i]['stale_bytes']),fresh_bytes=int(ring[i]['fresh_bytes']),sample_count=len(p),zero_sample_fraction=float(np.mean(p==0)),exact_match_recent_callbacks=matches))
(b/'ring_summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
