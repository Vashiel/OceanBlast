from pathlib import Path

import numpy as np, wave,json,csv

import matplotlib

matplotlib.use('Agg')

import matplotlib.pyplot as plt

from PIL import Image,ImageDraw

root=Path.cwd();base=root/'build/2026-10-17-gba-reference'

gba=np.memmap(base/'lcd.rgb',dtype=np.uint8,mode='r').reshape(-1,160,240,3)

digi=np.memmap(root/'build/2026-10-17-wade-reference-recording/lcd.rgb',dtype=np.uint8,mode='r').reshape(-1,160,240,3)

results={};fig,axes=plt.subplots(2,1,figsize=(11,5),layout='constrained')

for ax,(name,video,start,end) in zip(axes,[('GBA video',gba,12.5,22.0),('OceanBlast',digi,14.5,24.5)]):

    fps=23891/800 if name=='GBA video' else 30; i0=int(start*fps);i1=min(int(end*fps),len(video));roi=video[i0:i1,131:141,84:159,:].astype(float)

    score=((roi.min(axis=3)>195)&((roi.max(axis=3)-roi.min(axis=3))<45)).sum(axis=(1,2))

    centers=np.array([score.min(),score.max()],dtype=float)

    for _ in range(20):

        state=np.argmin(np.abs(score[:,None]-centers[None,:]),axis=1)

        centers=np.array([score[state==j].mean() for j in range(2)])

    on=state==np.argmax(centers);edges=np.flatnonzero(on[1:]!=on[:-1])+1

    times=(edges+i0)/fps

    rise=times[on[edges]];fall=times[~on[edges]]

    periods=np.concatenate([np.diff(rise),np.diff(fall)])

    results[name]=dict(window=[start,end],white_pixel_centers=centers.tolist(),transition_seconds=times.tolist(),rising_edges=rise.tolist(),falling_edges=fall.tolist(),periods=periods.tolist(),median_period=float(np.median(periods)) if len(periods) else None)

    ax.step((np.arange(len(on))+i0)/fps,on.astype(int),where='post',color='#2463a5');ax.set(title=name,ylabel='START sichtbar',yticks=[0,1],ylim=[-.15,1.15]);ax.grid(alpha=.2)

    canvas=Image.new('RGB',(480,190),'#eeeeee');draw=ImageDraw.Draw(canvas)

    for j in range(2):

        idx=i0+int(np.flatnonzero(state==j)[len(np.flatnonzero(state==j))//2]);canvas.paste(Image.fromarray(np.asarray(video[idx])),(j*240,20));draw.text((j*240+5,3),f'{name} t={idx/fps:.3f}',fill='black')

    canvas.save(base/(name.replace(' ','_')+'_blink_states.png'))

axes[-1].set_xlabel('Zeit der jeweiligen Aufnahme (s)');fig.savefig(base/'start_blink_comparison.png',dpi=160)

(base/'blink_measurements.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))

def read(path):

    with wave.open(str(path),'rb') as w:

        data=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)/32768

        return data.reshape(-1,w.getnchannels()).mean(axis=1),w.getframerate()

g,sr=read(base/'audio.wav');d,dsr=read(root/'build/2026-10-17-wade-pcm-diagnostic/wade-title-offline.wav')

window=220;usable=len(g)//window*window;grms=np.sqrt(np.mean(g[:usable].reshape(-1,window)**2,axis=1));print('GBA RMS per second:',[(i,round(float(np.sqrt(np.mean(g[i*sr:(i+1)*sr]**2))),4)) for i in range(9,24)])

fig,ax=plt.subplots(2,1,figsize=(12,5),layout='constrained')

for a,y,rate,start,end,title in [(ax[0],g,sr,10,24,'GBA-Referenz: Videozeit 10–24 s'),(ax[1],d,dsr,9,22.8112925,'OceanBlast: PCM-Zeit 9–22,81 s')]:

    left=int(start*rate);right=min(int(end*rate),len(y));step=220;x=y[left:right];n=len(x)//step

    lo=x[:n*step].reshape(n,step).min(axis=1);hi=x[:n*step].reshape(n,step).max(axis=1);t=start+np.arange(n)*step/rate

    a.fill_between(t,lo,hi,color='#316a9a',lw=0);a.set(title=title,ylabel='Amplitude',ylim=(-1,1));a.grid(alpha=.2)

ax[-1].set_xlabel('Zeit innerhalb der jeweiligen Quelle (s)');fig.savefig(base/'waveform_reference.png',dpi=160)
