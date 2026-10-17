from pathlib import Path
import numpy as np,csv,json,hashlib
b=Path('build/2026-10-17-wade-startup');rows=list(csv.DictReader((b/'current.csv').open()));raw=np.fromfile(b/'current.pcm','<i2')
first=next(k for k,r in enumerate(rows) if np.any(raw[int(r['sample_offset']):int(r['sample_offset'])+int(r['samples'])]))
t0=int(rows[first]['guest_ticks'])/20000000
out=[];pcm=0
for k,r in enumerate(rows):
    tick=int(r['guest_ticks']);duration=int(r['samples'])/2/int(r['input_rate']);delta=(tick-int(rows[k-1]['guest_ticks']))/20000000 if k else 0
    out.append(dict(callback=k,guest_seconds=tick/20000000,relative_music_seconds=tick/20000000-t0,pcm_seconds=pcm,block_duration_seconds=duration,delivery_interval_seconds=delta,interval_minus_block_seconds=delta-duration,rate=int(r['input_rate']),sample_count=int(r['samples']),source=int(r['source']),remaining=int(r['remaining'])))
    pcm+=duration
summary={'first_nonzero_callback':first,'first_nonzero_guest_seconds':t0,'first_nonzero_pcm_seconds':out[first]['pcm_seconds'],'windows':[]}
for lo,hi in [(0,2),(2,5),(5,10)]:
    part=[r for r in out if lo<=r['relative_music_seconds']<hi];e=[r['interval_minus_block_seconds'] for r in part]
    summary['windows'].append(dict(music_guest_window=[lo,hi],callbacks=len(part),max_interval_excess_seconds=max(e),min_interval_excess_seconds=min(e),total_interval_excess_seconds=sum(e),over_1ms=sum(v>.001 for v in e),rate_values=sorted(set(r['rate'] for r in part))))
summary['music_gaps_over_1ms']=[r for r in out[first:] if r['interval_minus_block_seconds']>.001]
summary['pcm_matches_previous']=hashlib.sha256((b/'current.pcm').read_bytes()).hexdigest()==hashlib.sha256(Path('build/2026-10-17-wade-pcm-diagnostic/current.pcm').read_bytes()).hexdigest()
(b/'timing.json').write_text(json.dumps(summary,indent=2)+'\n')
with (b/'callback_timing.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=out[0].keys());w.writeheader();w.writerows(out)
print(json.dumps(summary,indent=2))
