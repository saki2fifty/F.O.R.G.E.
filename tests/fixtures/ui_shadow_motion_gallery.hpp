#pragma once
#include <string_view>
namespace forge::test {
inline constexpr std::string_view shadow_motion_gallery = R"rml(<rml><head><style>
body,div,forge-shadow-panel,p,h1 {display:block;}
body {margin:0;width:100%;height:100%;font-family:Lato;font-size:15px;color:#e5f2ff;pointer-events:none;}
#lab {position:absolute;left:4%;top:2%;width:92%;}
h1 {font-size:23px;margin:0 0 3px;color:#79e7cf;} p {margin:3px 0;}
#grid {margin-top:8px;} .card {display:inline-block;width:30%;margin:0 2% 18px 0;vertical-align:top;}
.label {height:22px;font-size:13px;} .sample {position:relative;height:44px;border-radius:7px;background-color:#30536e;}
.word {position:absolute;left:12%;top:13px;font-size:14px;color:white;}
forge-shadow-panel {forge-shadow-x:5px;forge-shadow-y:5px;forge-shadow-blur:6px;forge-shadow-color:#000b;}
#pulse {animation:2s linear-in infinite alternate pulse;}
@keyframes pulse {
 from {forge-shadow-x:0px;forge-shadow-y:0px;forge-shadow-blur:2px;forge-shadow-spread:0px;forge-shadow-color:#79e7cf80;}
 to {forge-shadow-x:8px;forge-shadow-y:6px;forge-shadow-blur:14px;forge-shadow-spread:3px;forge-shadow-color:#e96644c0;}
}
#hover {pointer-events:auto;transition:forge-shadow-y .3s cubic-in-out,forge-shadow-blur .3s cubic-in-out,forge-shadow-color .3s cubic-in-out;}
#hover:hover,#hover:focus {forge-shadow-y:10px;forge-shadow-blur:16px;forge-shadow-color:#79e7cfb0;}
#inset {forge-shadow-inset:inset;animation:2s linear-in infinite alternate inside;}
@keyframes inside {from {forge-shadow-x:2px;forge-shadow-blur:2px;} to {forge-shadow-x:12px;forge-shadow-blur:12px;}}
#switch {animation:2s linear-in infinite alternate switch;forge-shadow-color:#e96644a0;}
@keyframes switch {from {forge-shadow-inset:outset;} to {forge-shadow-inset:inset;}}
button {display:inline-block;padding:5px 8px;margin:4px 5px 0 0;background-color:#30536e;pointer-events:auto;}
button:hover,button:focus {background-color:#4c849e;} .hint {font-size:13px;color:#adc7db;}
</style></head><body><div id="lab"><h1>Animated UI shadows</h1><p>Native keyframes, hover transitions and live values.</p><div id="grid">
<div class="card"><div class="label">Reference</div><div class="sample"><div class="word">Static surface</div></div></div>
<div class="card"><div class="label">Color / offset</div><forge-shadow-panel id="pulse" class="sample"><div class="word">Keyframes</div></forge-shadow-panel></div>
<div class="card"><div class="label">Hover / focus</div><forge-shadow-panel id="hover" class="sample" tabindex="0"><div class="word">Point here</div></forge-shadow-panel></div>
<div class="card"><div class="label">Inset motion</div><forge-shadow-panel id="inset" class="sample"><div class="word">Inner edge</div></forge-shadow-panel></div>
<div class="card"><div class="label">Discrete inset</div><forge-shadow-panel id="switch" class="sample"><div class="word">In / out</div></forge-shadow-panel></div>
<div class="card"><div class="label">Live / {{blur}} px</div><forge-shadow-panel id="live" class="sample" data-style-forge-shadow-blur="blur + 'px'"><div class="word">UI Values</div></forge-shadow-panel></div>
</div><p class="hint">UI Values / blur: 0..32. Tick: {{tick}}</p><p class="hint">Shadow motion continues while gameplay is paused.</p>
<button id="pause" data-event-click="command('Pause')">Pause</button><button id="resume" data-event-click="command('Resume')">Resume</button><button id="step" data-event-click="command('Step')">Step</button>
</div></body></rml>)rml";
}
