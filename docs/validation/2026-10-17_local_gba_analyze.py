from pathlib import Path
import json,csv,wave,numpy as np,hashlib
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path.cwd();b=root/'build/2026-10-17-local-mgba-reference';fps=23891/400
videos=[('mGBA',b/'lcd-first12.rgb',fps,0,10),('OceanBlast',root/'build/2026-10-17-wade-reference-recording/lcd.rgb',30,14.5,24.5)]
meta={};fig,axs=plt.subplots(2,1,figsize=(11,5),layout='constrained')
for ax,(name,path,rate,start,end) in zip(axs,videos):
    vid=np.memmap(path,np.uint8,'r').reshape(-1,160,240,3);lo=int(start*rate);hi=min(int(end*rate),len(vid));roi=vid[lo:hi,131:141,84:159].astype(float)
    s=((roi.min(axis=3)>195)&((roi.max(axis=3)-roi.min(axis=3))<45)).sum(axis=(1,2));c=np.array([s.min(),s.max()],float)
    for _ in range(20):z=np.argmin(abs(s[:,None]-c[None,:]),axis=1);c=np.array([s[z==j].mean() for j in range(2)])
    on=z==c.argmax();edges=np.flatnonzero(on[1:]!=on[:-1])+1;rise=(edges[on[edges]]+lo)/rate;fall=(edges[~on[edges]]+lo)/rate
    slopes=[float(np.polyfit(np.arange(len(e)),e,1)[0]) for e in [rise,fall]]
    meta[name]=dict(fps=rate,window=[start,end],rising_edges=rise.tolist(),falling_edges=fall.tolist(),fitted_period=float(np.mean(slopes)),white_pixel_centers=c.tolist())
    ax.step((np.arange(len(on))+lo)/rate,on.astype(int),where='post');ax.set(title=name,ylabel='START visible',yticks=[0,1],ylim=[-.1,1.1]);ax.grid(alpha=.2)
axs[-1].set_xlabel('Seconds in each capture');fig.savefig(b/'blink_comparison.png',dpi=160)
def read(path):
    with wave.open(str(path),'rb') as w:
        x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)/32768;return x.reshape(-1,w.getnchannels()).mean(axis=1),w.getframerate()
g,sr=read(b/'audio.wav');d,dr=read(root/'build/2026-10-17-wade-pcm-diagnostic/wade-title-offline.wav')
def rms(x,rate):
    block=round(rate*.01);n=len(x)//block;return (np.arange(n)+.5)*block/rate,np.sqrt(np.mean(x[:n*block].reshape(n,block)**2,axis=1))
gt,ge=rms(g,sr);dt,de=rms(d,dr);silent=ge<.0001;runs=[];start=None
for i,v in enumerate(silent):
    if v and start is None:start=i
    if start is not None and (not v or i==len(silent)-1):
        if gt[i]-gt[start]>.3:runs.append([float(gt[start]),float(gt[i])])
        start=None
print('Silence intervals:',runs)
reset=max([r for r in runs if r[0]>25 and r[1]<90],key=lambda r:r[1]-r[0]);gs=float(gt[np.flatnonzero((gt>reset[1]-.05)&(ge>.01))[0]]);ds=float(dt[np.flatnonzero(de>.005)[0]])
meta['audio']=dict(reset_silence=reset,mgba_music_start=gs,oceanblast_pcm_music_start=ds,mgba_source_rate=65536,analysis_rate=sr,mgba_duration=len(g)/sr)
fig,axs=plt.subplots(2,1,figsize=(12,5),layout='constrained')
for ax,x,rate,onset,name in [(axs[0],g,sr,gs,'GBA in local mGBA 0.10.5'),(axs[1],d,dr,ds,'OceanBlast generated PCM')]:
    clip=x[int(onset*rate):int((onset+12)*rate)];block=220;n=len(clip)//block;bands=clip[:n*block].reshape(n,block);t=np.arange(n)*block/rate
    ax.fill_between(t,bands.min(axis=1),bands.max(axis=1),color='#2b6b9e',lw=0);ax.set(title=name,ylabel='Amplitude',ylim=(-1,1));ax.grid(alpha=.2)
    with wave.open(str(b/(name.split()[0].lower()+'-music.wav')),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(rate);w.writeframes(np.clip(clip*32768,-32768,32767).astype('<i2').tobytes())
axs[-1].set_xlabel('Seconds after music onset; no time stretching');fig.savefig(b/'waveform_comparison.png',dpi=160)
t=np.arange(.15,10,.02);de=np.convolve(de,np.ones(5)/5,'same');ge=np.convolve(ge,np.ones(5)/5,'same');dv=np.interp(ds+t,dt,de);dv=(dv-dv.mean())/dv.std();best=(-1,None,None)
for scale in np.arange(.65,1.401,.005):
    for offset in np.arange(-1.5,1.501,.02):
        gv=np.interp(gs+offset+scale*t,gt,ge);gv=(gv-gv.mean())/gv.std();c=float(np.mean(gv*dv))
        if c>best[0]:best=(c,float(scale),float(offset))
meta['audio']['rms_envelope_fit']=dict(correlation=best[0],reference_seconds_per_digi_second=best[1],reference_offset_seconds=best[2],scale_search=[.65,1.4],offset_search=[-1.5,1.5])
meta['blink_rate_ratio']=meta['mGBA']['fitted_period']/meta['OceanBlast']['fitted_period'];(b/'measurements.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta,indent=2))
