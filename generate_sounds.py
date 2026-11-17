from pathlib import Path
import wave,math,struct,shutil
out=Path('sounds');out.mkdir(exist_ok=True)
for name in ['Godlike','Ownage']:
    if not (out/(name+'.wav')).is_file():
        raise SystemExit('Keep the bundled '+name+'.wav; see SOUND_CREDITS.txt for its source.')
# Original synthesized effects, released under CC0; no game samples.
for name,notes in {'wpn_moveselect':[(900,.025)],'wpn_select':[(700,.035),(1100,.045)],'wpn_denyselect':[(180,.06),(130,.07)],'wpn_hudon':[(400,.04),(650,.04),(950,.05)],'wpn_hudoff':[(800,.04),(500,.04),(300,.05)],'error':[(170,.06),(120,.08)],'tick':[(1200,.018)],'success':[(660,.07),(880,.07),(1320,.13)],'achievement':[(523,.09),(659,.09),(784,.09),(1046,.24)]}.items():
    samples=[]
    for freq,dur in notes:
        n=int(44100*dur)
        for i in range(n):
            t=i/44100;env=min(1,i/150)*((1-i/n)**1.7)
            val=(math.sin(2*math.pi*freq*t)+.22*math.sin(2*math.pi*freq*3*t))*env*.24
            samples.append(struct.pack('<h',int(32767*val)))
    with wave.open(str(out/(name+'.wav')),'wb') as w:w.setparams((1,2,44100,0,'NONE','not compressed'));w.writeframes(b''.join(samples))
