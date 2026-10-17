from pathlib import Path
import numpy as np,json,wave,csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
b=Path('build/2026-10-17-wade-startup')
with wave.open('build/2026-10-17-digiblast-soundtrack-reference/audio.wav','rb') as w:
    ref=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)/32768;sr=w.getframerate()
def env(x):
    n=441;count=len(x)//n;t=(np.arange(count)+.5)*.02;v=np.sqrt(np.mean(x[:count*n].reshape(count,n)**2,axis=1));return t,np.convolve(v,np.ones(3)/3,'same')
rt,re=env(ref);result={};fig,axs=plt.subplots(2,1,figsize=(11,6),layout='constrained')
for ax,name in zip(axs,['current','ratio12']):
    raw=np.fromfile(b/(name+'.converted.pcm'),'<i2').reshape(-1,2).mean(axis=1)/32768
    onset=np.flatnonzero(abs(raw)>.0001)[0]/22050;dt,de=env(raw);end=min(9.8,len(raw)/22050-onset-.1);t=np.arange(2,end,.02);dv=np.interp(onset+t,dt,de);best=(-1,0,0)
    for scale in np.arange(.97,1.031,.001):
        for off in np.arange(0,40,.02):
            if off+scale*end>rt[-1]:continue
            gv=np.interp(off+scale*t,rt,re);c=float(np.corrcoef(dv,gv)[0,1])
            if c>best[0]:best=(c,float(scale),float(off))
    windows=[]
    for lo,hi in [(0,.5),(.5,1),(1,2),(2,end)]:
        t=np.arange(lo,hi,.01);a=np.interp(onset+t,dt,de);c=np.interp(best[2]+best[1]*t,rt,re);windows.append(dict(seconds=[lo,hi],correlation=float(np.corrcoef(a,c)[0,1]),mean_rms_ratio=float(a.mean()/c.mean())))
    result[name]=dict(onset_pcm_seconds=onset,available_music_seconds=len(raw)/22050-onset,body_fit=dict(correlation=best[0],reference_seconds_per_pcm_second=best[1],reference_offset_seconds=best[2]),windows=windows)
    t=np.arange(0,min(4,end),.02);a=np.interp(onset+t,dt,de);c=np.interp(best[2]+best[1]*t,rt,re);factor=np.mean(a[t>=2])/np.mean(c[t>=2]);ax.plot(t,a,label=name+' generated PCM');ax.plot(t,c*factor,label='XM conversion, level matched after second 2',alpha=.8);ax.legend();ax.set(ylabel='RMS amplitude');ax.grid(alpha=.2)
axs[-1].set_xlabel('Seconds after first nonzero PCM');fig.savefig(b/'startup_budget_comparison.png',dpi=160)
(b/'budget_comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
