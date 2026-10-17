exec(open('build/2026-10-17-gba-reference/align_audio.py',encoding='utf-8').read().split('gt,ge=env')[0])
from numpy.lib.stride_tricks import sliding_window_view
meta=json.loads((b/'reference_measurements.json').read_text());gs=meta['gba_music_onset_threshold_seconds'];ds=meta['digi_music_onset_threshold_seconds']
def chroma(y,rate):
    nfft=4096;hop=256;frames=sliding_window_view(y,nfft)[::hop];sp=np.abs(np.fft.rfft(frames*np.hanning(nfft),axis=1))
    f=np.fft.rfftfreq(nfft,1/rate);valid=(f>80)&(f<6000);classes=np.rint(69+12*np.log2(f[valid]/440)).astype(int)%12
    c=np.stack([sp[:,valid][:,classes==i].sum(axis=1) for i in range(12)],axis=1)
    c=np.log1p(c);c-=c.mean(axis=0);c-=c.mean(axis=1,keepdims=True);c/=np.maximum(np.linalg.norm(c,axis=1,keepdims=True),1e-12)
    return (np.arange(len(c))*hop+nfft/2)/rate,c
# Keep a small fixed music interval; do not include the earlier boot silence.
gleft=int((gs-1)*sr);gright=int((gs+15)*sr);ctg,cg=chroma(g[gleft:gright],sr);ctg+=gleft/sr
dleft=int(max(0,ds-.2)*dsr);ctd,cd=chroma(d[dleft:],dsr);ctd+=dleft/dsr
t=np.arange(.25,10,.03);dv=np.stack([np.interp(ds+t,ctd,cd[:,j]) for j in range(12)],axis=1);dv/=np.maximum(np.linalg.norm(dv,axis=1,keepdims=True),1e-12)
best=(-1,None,None)
for scale in np.arange(.65,1.401,.005):
    for offset in np.arange(-.5,.501,.02):
        gv=np.stack([np.interp(gs+offset+scale*t,ctg,cg[:,j]) for j in range(12)],axis=1);gv/=np.maximum(np.linalg.norm(gv,axis=1,keepdims=True),1e-12)
        score=float(np.mean(np.sum(gv*dv,axis=1)))
        if score>best[0]:best=(score,float(scale),float(offset))
meta['pitch_class_comparison']=dict(best_centered_cosine_similarity=best[0],reference_seconds_per_digi_second=best[1],reference_offset_seconds=best[2],scale_search=[.65,1.4],offset_search=[-.5,.5],analysis_window_digi_seconds=[.25,10],note='Exploratory cross-platform spectral comparison; no validated tempo calibration.')
(b/'reference_measurements.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta['pitch_class_comparison'],indent=2))
fig,ax=plt.subplots(2,1,figsize=(12,6),layout='constrained')
for a,y,rate,onset,title in [(ax[0],g,sr,gs,'GBA reference'),(ax[1],d,dsr,ds,'OceanBlast generated PCM')]:
    cut=y[int(onset*rate):int((onset+12)*rate)];a.specgram(cut,NFFT=2048,Fs=rate,noverlap=1536,cmap='magma',vmin=-100,vmax=-25);a.set(ylim=(0,5000),title=title,ylabel='Frequency (Hz)')
ax[-1].set_xlabel('Seconds after detected music onset (no time stretching)');fig.savefig(b/'spectrogram_reference.png',dpi=160)
