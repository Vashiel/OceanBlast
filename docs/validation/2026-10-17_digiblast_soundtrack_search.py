from pathlib import Path
import json,wave,numpy as np
root=Path.cwd();b=root/'build/2026-10-17-digiblast-soundtrack-reference';m={'audio':{'reference_origin_seconds':0,'oceanblast_pcm_music_start':10.211791383219955}}
def read(p):
    with wave.open(str(p),'rb') as w:
        x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)/32768;return x.reshape(-1,w.getnchannels()).mean(axis=1),w.getframerate()
g,sr=read(b/'audio.wav');d,dr=read(root/'build/2026-10-17-wade-pcm-diagnostic/wade-title-offline.wav');gs=m['audio']['reference_origin_seconds'];ds=m['audio']['oceanblast_pcm_music_start']
def env(x,rate):
    n=round(rate*.02);count=len(x)//n;t=(np.arange(count)+.5)*n/rate;y=np.sqrt(np.mean(x[:count*n].reshape(count,n)**2,axis=1));return t,np.convolve(y,np.ones(3)/3,'same')
gt,ge=env(g,sr);dt,de=env(d,dr);reference=np.interp(gs+np.arange(0,len(g)/sr-gs-.1,.02),gt,ge);best=(-1,None,None)
for scale in np.arange(.5,2.001,.005):
    times=np.arange(0,9.8*scale,.02);template=np.interp(ds+.2+times/scale,dt,de);template-=template.mean();norm=np.linalg.norm(template);n=len(template)
    sums=np.convolve(reference,np.ones(n),'valid');squares=np.convolve(reference**2,np.ones(n),'valid');denom=np.sqrt(np.maximum(squares-sums*sums/n,1e-20))*norm
    scores=np.correlate(reference,template,'valid')/denom;i=int(scores.argmax());score=float(scores[i])
    if score>best[0]:best=(score,float(scale),float(i*.02))
m['audio']['broad_envelope_alignment']=dict(correlation=best[0],reference_seconds_per_digi_second=best[1],reference_offset_seconds=best[2],scale_search=[.5,2],reference_search_seconds=[0,len(reference)*.02],digi_interval_seconds=[.2,10],method='Normalized sliding RMS-envelope correlation; exploratory, not proof of pitch or emulation accuracy.')
(b/'measurements.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m['audio']['broad_envelope_alignment'],indent=2))
