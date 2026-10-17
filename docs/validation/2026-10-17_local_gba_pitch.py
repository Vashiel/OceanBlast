exec(open('build/2026-10-17-local-mgba-reference/audio_search.py').read().split('for scale in')[0])
fit=m['audio']['split_window_alignment'][-1];s=fit['reference_seconds_per_digi_second'];off=fit['reference_intercept_seconds']
n=4096;freq=np.fft.rfftfreq(n,1/sr);mask=(freq>150)&(freq<7000);f=freq[mask];win=np.hanning(n);times=np.arange(2,9.9,.1)
def spec(x,t,rate):
    i=round(t*rate)-n//2;v=np.log1p(np.abs(np.fft.rfft(x[i:i+n]*win))*100)
    # Remove broad spectral colour before comparing local peaks.
    return v-np.convolve(v,np.ones(31)/31,'same')
dv=np.array([spec(d,ds+t,dr)[mask] for t in times]);gv=np.array([spec(g,gs+off+s*t,sr) for t in times])
results=[]
for ratio in np.arange(.85,1.201,.001):
    aligned=np.array([np.interp(f/ratio,freq,v) for v in gv]);scores=[]
    for a,c in zip(dv,aligned):scores.append(float(np.corrcoef(a,c)[0,1]))
    results.append((float(np.median(scores)),float(ratio)))
best=max(results);m['audio']['spectral_peak_alignment']=dict(median_correlation=best[0],oceanblast_frequency_over_gba_frequency=best[1],ratio_search=[.85,1.2],method='4096-sample Hann windows, log magnitude minus 31-bin local mean, 150-7000 Hz; median across fixed envelope-aligned times 2-9.9 seconds. Exploratory cross-version spectral-peak correspondence, not listening acceptance.',unshifted_median_correlation=min(results,key=lambda x:abs(x[1]-1))[0])
(b/'measurements.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m['audio']['spectral_peak_alignment'],indent=2))
