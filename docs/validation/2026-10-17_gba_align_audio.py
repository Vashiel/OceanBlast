from pathlib import Path
import numpy as np,wave,json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path.cwd();b=root/'build/2026-10-17-gba-reference'
def read(p):
    with wave.open(str(p),'rb') as w:
        x=np.frombuffer(w.readframes(w.getnframes()),'<i2').astype(float)/32768
        return x.reshape(-1,w.getnchannels()).mean(axis=1),w.getframerate()
g,sr=read(b/'audio.wav');d,dsr=read(root/'build/2026-10-17-wade-pcm-diagnostic/wade-title-offline.wav')
def env(y,rate):
    block=round(rate*.01);n=len(y)//block
    e=np.sqrt(np.mean(y[:n*block].reshape(n,block)**2,axis=1));t=(np.arange(n)+.5)*block/rate
    return t,np.convolve(e,np.ones(5)/5,mode='same')
gt,ge=env(g,sr);dt,de=env(d,dsr)
gi=np.flatnonzero((gt>10)&(ge>.01))[0];di=np.flatnonzero(de>.005)[0];gstart=float(gt[gi]);dstart=float(dt[di])
t=np.arange(.15,10.0,.02);dv=np.interp(dstart+t,dt,de);dv=(dv-dv.mean())/dv.std()
best=(-1,None,None)
for scale in np.arange(.8,1.201,.002):
    for offset in np.arange(-.4,.401,.01):
        gv=np.interp(gstart+offset+scale*t,gt,ge);gv=(gv-gv.mean())/gv.std()
        c=float(np.mean(gv*dv))
        if c>best[0]:best=(c,float(scale),float(offset))
blink=json.loads((b/'blink_measurements.json').read_text())
for name,v in blink.items():
    slopes=[]
    for edges in [v['rising_edges'],v['falling_edges']]:
        slopes.append(float(np.polyfit(np.arange(len(edges)),np.array(edges),1)[0]))
    v['fitted_period_seconds']=float(np.mean(slopes))
ratio=blink['GBA video']['fitted_period_seconds']/blink['OceanBlast']['fitted_period_seconds']
meta=dict(gba_music_onset_threshold_seconds=gstart,digi_music_onset_threshold_seconds=dstart,rms_envelope_best_correlation=best[0],reference_seconds_per_digi_second=best[1],reference_onset_adjustment_seconds=best[2],envelope_window_digi_seconds=[.15,10.0],search_scale_range=[.8,1.2],search_offset_seconds=[-.4,.4],blink=blink,blink_rate_ratio_digi_to_reference=ratio,limitation='GBA recording is a cross-platform reference, not a calibrated digiBLAST hardware capture; envelope fit is descriptive, not proof of matching pitch or correct emulation.')
(b/'reference_measurements.json').write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps({k:v for k,v in meta.items() if k!='blink'},indent=2));print('Blink fitted periods:',[(k,v['fitted_period_seconds']) for k,v in blink.items()])
fig,ax=plt.subplots(2,1,figsize=(12,6),layout='constrained')
for a,(yy,rate,onset,title) in zip(ax,[(g,sr,gstart,'GBA video: music onset aligned to 0 s'),(d,dsr,dstart,'OceanBlast generated PCM: music onset aligned to 0 s')]):
    start=int(onset*rate);stop=min(start+int(12*rate),len(yy));x=yy[start:stop];step=220;n=len(x)//step;xx=x[:n*step].reshape(n,step);tt=np.arange(n)*step/rate
    a.fill_between(tt,xx.min(axis=1),xx.max(axis=1),color='#2b6b9e',lw=0);a.set(title=title,ylabel='Amplitude',ylim=(-1,1));a.grid(alpha=.2)
ax[-1].set_xlabel('Seconds after estimated music onset (no time stretching)');fig.savefig(b/'waveforms_onset_aligned.png',dpi=160)
fig,ax=plt.subplots(figsize=(12,3.5),layout='constrained')
ax.plot(t,dv,label='OceanBlast normalized RMS',color='#2463a5');gv=np.interp(gstart+best[2]+best[1]*t,gt,ge);gv=(gv-gv.mean())/gv.std();ax.plot(t,gv,label=f'GBA RMS after descriptive alignment (r={best[0]:.3f})',alpha=.8,color='#cf7727');ax.set(xlabel='OceanBlast seconds after onset',ylabel='Normalized envelope',title='Envelope comparison; alignment is not a hardware-speed calibration');ax.grid(alpha=.2);ax.legend();fig.savefig(b/'envelope_alignment.png',dpi=160)
