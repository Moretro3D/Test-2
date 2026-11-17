/* Installation USB des 386 cris legacy. Les fichiers source sont recuperes
   directement depuis PokeAPI, convertis localement dans le navigateur puis
   envoyes en WAV PCM 16 kHz sur la microSD. Aucun cri n'est stocke sur GitHub Pages. */
(function(){
  const CRY_BASE='https://raw.githubusercontent.com/PokeAPI/cries/main/cries/pokemon/legacy/';
  const TARGET_RATE=16000;
  const cryEncoder=new TextEncoder();

  function putAscii(view, offset, text){
    for(let i=0;i<text.length;i++) view.setUint8(offset+i,text.charCodeAt(i));
  }

  function audioBufferToWav16(audio){
    const srcRate=audio.sampleRate;
    const frames=Math.min(Math.ceil(audio.duration*TARGET_RATE),TARGET_RATE*4);
    const samples=new Float32Array(frames);
    const channels=[];
    for(let c=0;c<audio.numberOfChannels;c++) channels.push(audio.getChannelData(c));
    let peak=0;
    for(let i=0;i<frames;i++){
      const pos=i*srcRate/TARGET_RATE, a=Math.floor(pos), b=Math.min(a+1,audio.length-1), t=pos-a;
      let value=0;
      for(const ch of channels) value+=(ch[a]||0)*(1-t)+(ch[b]||0)*t;
      value/=channels.length||1;
      samples[i]=Math.max(-1,Math.min(1,value));
      peak=Math.max(peak,Math.abs(samples[i]));
    }
    const gain=peak>0.94?0.94/peak:1;
    const fade=Math.min(96,Math.floor(frames/8));
    const out=new ArrayBuffer(44+frames*2), view=new DataView(out);
    putAscii(view,0,'RIFF'); view.setUint32(4,36+frames*2,true); putAscii(view,8,'WAVE');
    putAscii(view,12,'fmt '); view.setUint32(16,16,true); view.setUint16(20,1,true);
    view.setUint16(22,1,true); view.setUint32(24,TARGET_RATE,true); view.setUint32(28,TARGET_RATE*2,true);
    view.setUint16(32,2,true); view.setUint16(34,16,true); putAscii(view,36,'data'); view.setUint32(40,frames*2,true);
    for(let i=0;i<frames;i++){
      let envelope=1;
      if(i<fade) envelope=i/fade;
      else if(i>=frames-fade) envelope=(frames-1-i)/fade;
      const s=Math.max(-1,Math.min(1,samples[i]*gain*envelope));
      view.setInt16(44+i*2,s<0?s*32768:s*32767,true);
    }
    return new Uint8Array(out);
  }

  async function fetchCry(id,ctx){
    let lastError;
    for(let attempt=1;attempt<=3;attempt++){
      try{
        const response=await fetch(CRY_BASE+id+'.ogg',{cache:'force-cache'});
        if(!response.ok) throw new Error('HTTP '+response.status);
        const decoded=await ctx.decodeAudioData(await response.arrayBuffer());
        return audioBufferToWav16(decoded);
      }catch(error){ lastError=error; if(attempt<3) await new Promise(r=>setTimeout(r,350*attempt)); }
    }
    throw lastError;
  }

  window.installAuthenticCries=async function(options){
    options=options||{};
    const button=options.button||document.getElementById('cries');
    const cryWriter=options.writer||window.writer;
    const cryWait=options.waitFor||window.waitFor;
    const crySend=options.sendOne||window.sendOne;
    const cryLog=options.log||window.log;
    const controls=options.controls||[document.getElementById('auto'),document.getElementById('music')];
    const setControls=disabled=>controls.filter(Boolean).forEach(control=>control.disabled=disabled);
    if(!cryWriter){ cryLog('Connecte d’abord le PokeTama.'); return; }
    button.disabled=true;
    setControls(true);
    const bar=options.bar||document.getElementById('bar'),fill=bar?(bar.firstElementChild||bar):null;
    if(bar) bar.style.display='block'; if(fill){ fill.style.width='0%'; fill.value=0; }
    let ctx=null;
    try{
      cryLog('Préparation du dossier /cries sans toucher aux sauvegardes…');
      await cryWriter.write(cryEncoder.encode('PREPARE_CRIES\n'));
      if(!await cryWait('READY',120000)) throw new Error('microSD non prête ou réponse USB absente');
      if(fill){ fill.style.width='1%'; fill.value=1; }
      const AudioCtor=window.AudioContext||window.webkitAudioContext;
      if(!AudioCtor) throw new Error('conversion audio indisponible : utilise Chrome ou Edge');
      ctx=new AudioCtor();
      cryLog('Téléchargement, conversion et copie des 386 cris fidèles…');
      for(let id=1;id<=386;id++){
        const wav=await fetchCry(id,ctx);
        const name='cries/'+String(id).padStart(3,'0')+'.wav';
        if(!await crySend(name,wav)) throw new Error('transfert interrompu sur '+name);
        if(fill){ fill.style.width=(id/386*100).toFixed(1)+'%'; fill.value=id/386*100; }
        if(id===1||id%10===0||id===386) cryLog('Cris installés : '+id+'/386');
      }
      cryLog('386/386 cris installés. Redémarre le PokeTama avec le bouton PWR.');
    }catch(error){
      cryLog('ÉCHEC CRIS : '+error.message+'. Relance le bouton pour recommencer proprement.');
    }finally{
      if(ctx) await ctx.close();
      button.disabled=false; setControls(false);
    }
  };
})();
