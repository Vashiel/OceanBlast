exec(open('build/2026-10-17-digiblast-soundtrack-reference/search.py').read().split('for scale in')[0])
m=json.loads((b/'measurements.json').read_text())
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fit=m['audio']['broad_envelope_alignment']; scale=fit['reference_seconds_per_digi_second']; intercept=fit['reference_offset_seconds']-.2*scale
windows=[]
for lo,hi in [(.2,5),(5,10),(.2,10)]:
    times=np.arange(lo,hi,.02); dv=np.interp(ds+times,dt,de); top=(-1,0,0)
    for s in np.arange(.97,1.031,.001):
        for off in np.arange(intercept-.3,intercept+.301,.01):
            gv=np.interp(gs+off+s*times,gt,ge); corr=float(np.corrcoef(dv,gv)[0,1])
            if corr>top[0]:top=(corr,float(s),float(off))
    windows.append(dict(digi_seconds=[lo,hi],correlation=top[0],reference_seconds_per_digi_second=top[1],reference_intercept_seconds=top[2]))
t=np.arange(.2,10,.02); dv=np.interp(ds+t,dt,de); gv=np.interp(gs+intercept+scale*t,gt,ge)
fig,ax=plt.subplots(figsize=(11,4),layout='constrained')
ax.plot(t,dv/dv.max(),label='OceanBlast generated PCM',lw=1.5);ax.plot(t,gv/gv.max(),label='digiBLAST XM conversion, aligned at 1.000 reference seconds per second',alpha=.8,lw=1)
ax.set(xlabel='OceanBlast PCM seconds after music onset',ylabel='Normalized RMS envelope',title='Wade title music: corresponding rhythmic structure');ax.legend();ax.grid(alpha=.2);fig.savefig(b/'aligned_envelope.png',dpi=160)
np.savetxt(b/'aligned_envelope.csv',np.column_stack([t,dv,gv]),delimiter=',',header='oceanblast_seconds_after_music_onset,oceanblast_rms,reference_aligned_rms',comments='')
m['audio']['split_window_alignment']=windows
(b/'measurements.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(windows,indent=2))
