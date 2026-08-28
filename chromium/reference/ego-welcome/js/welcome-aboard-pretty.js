import{j as e,r as l,d as V,z as F}from"./vendor-react-UOJBPTs6.js";import{ba as q,a5 as Q,i as K,j as Z,aT as W,k as X,au as J,av as Y,aW as $,e as ee,f as se,b6 as te,T as ae}from"./vendor-ui-BW7Nyrti.js";import{aq as N}from"./vendor-misc-Dbd9poHC.js";import{c as f}from"./utils-sXEavXLF.js";import"./index-9hL7Aems.js";import"./vendor-radix-CZ9oksdH.js";import"./vendor-data-DxVYmB55.js";import"./runtime-preload-D8_b-Rn0.js";const U="phc_AMs6vxfhoPCeaN6cJ94ZqNc4V7J8Ps4qbcN7RpuRue7p".trim(),le="https://us.i.posthog.com".trim()||"https://us.i.posthog.com";let k=!1,B;function re(){return!!(B||U)}function ne(s={}){const a=s.key?.trim()||U;return a?(k||(N.init(a,{api_host:le,autocapture:!1,capture_pageview:!1,disable_session_recording:!0,capture_heatmaps:!1,capture_dead_clicks:!1,capture_performance:!1,advanced_disable_flags:!0,loaded(n){n.debug()},person_profiles:"identified_only"}),k=!0,B=a),N):null}const ie="phc_spJGBDN5uNCMnQtbLn9A4Fnj7AyHaxcscv7ppC9wMM9k".trim(),oe={page:"welcome_aboard"},A={PageView:"$pageview",PromptCopied:"prompt_copied",CardClicked:"card_clicked"};function _(s,a){re()&&N.capture(s,a)}function ce(){const s=ne({key:ie});s&&(s.register(oe),s.capture(A.PageView))}function de(s,a){_(A.PromptCopied,{command:s,prompt:a})}function w(s,a){_(A.CardClicked,{card:s,url:a})}const pe=""+new URL("../assets/icon-career-DVkqCTKo.png",import.meta.url).href,xe=""+new URL("../assets/icon-finance-BXNAVG4F.png",import.meta.url).href,me=""+new URL("../assets/icon-house-DG8A1Rr3.png",import.meta.url).href,he=""+new URL("../assets/icon-research-DlBO2QXe.png",import.meta.url).href,fe=""+new URL("../assets/tutorial-CLOwrZhx.png",import.meta.url).href,E="https://lite.ego.app/use-cases",M="https://lite.ego.app/document/en/docs/video-run-your-first-task",R="https://lite.ego.app/document/en/docs/quick-start",ue=[{src:he,alt:"Research"},{src:pe,alt:"Career"},{src:me,alt:"Home"},{src:xe,alt:"Finance"}],ge=["Analyze Competitors' Activity","Find Yourself the Best Job","Track Your Stocks in One Pass"];function b({
   className:s}){return e.jsx("div",{"aria-hidden":!0,
   className:f("pointer-events-none absolute inset-0 rounded-[inherit] opacity-0 shadow-[inset_0_0_6px_0_var(--accent),inset_0_0_32px_0_var(--accent)] transition-opacity duration-150 [corner-shape:superellipse(1.4)] group-hover:opacity-100",s)})}function we(){return e.jsxs("section",{
   className:"relative h-120 w-118 shrink-0 overflow-hidden rounded-4xl bg-(--bg-3) [corner-shape:superellipse(1.4)]",
children:[e.jsx("header",{
   className:"flex flex-col px-10 py-8",children:e.jsxs("h2",{
   className:"font-semibold text-(--text-1) text-[28px] leading-[1.15]",
children:["Discover how ego ",e.jsx("span",{
   className:"font-light italic",children:"(lite)"})," can enhance your life"]})}),
  e.jsxs("div",{
   className:"absolute inset-x-0 bottom-0 grid grid-cols-2 gap-2 p-2",
children:[e.jsxs("a",{href:M,target:"_blank",rel:"noopener noreferrer",onClick:()=>w("tutorial",M),
   className:"group relative flex h-35 flex-col justify-end overflow-hidden rounded-3xl bg-(--fg-1) px-3 py-2.5 [corner-shape:superellipse(1.4)]",
children:[e.jsx("img",{src:fe,alt:"",
   className:"pointer-events-none absolute inset-0 size-full object-cover object-bottom-right"}),
  e.jsx("span",{
   className:"relative inline-flex w-fit items-center justify-center rounded-full bg-(--fg-3) px-1.5 py-1 font-medium text-(--text-3) text-[12px] leading-3.5",children:"Tutorial"}),
  e.jsx(b,{
   className:"mix-blend-plus-lighter"})]}),
  e.jsxs("a",{href:R,target:"_blank",rel:"noopener noreferrer",onClick:()=>w("docs",R),
   className:"group relative flex h-35 flex-col justify-end overflow-hidden rounded-3xl bg-(--fg-1) px-3 py-2.5 [corner-shape:superellipse(1.4)]",
children:[e.jsxs("div",{
   className:"pointer-events-none absolute top-5 left-1/2 flex w-47 -translate-x-1/2 flex-col gap-2.25",
children:[e.jsx("p",{
   className:"font-bold text-(--text-1) text-[18px] leading-5.25",children:"Quick start"}),
  e.jsx("p",{
   className:"font-medium text-(--text-3) text-[6px] leading-2",children:"Two minutes to get your Codex, Claude Codgent working in the browser for you."}),
  e.jsx("p",{
   className:"font-medium text-(--text-2) text-[5px] leading-2",children:"ego lite is a browser built for both you rome, it's based on Chromium, so you don't have to change a tu browse - your extensions,"})]}),
  e.jsx("span",{
   className:"relative inline-flex w-fit items-center justify-center rounded-full bg-(--fg-3) px-1.5 py-1 font-medium text-(--text-3) text-[12px] leading-3.5",children:"Docs"}),
  e.jsx(b,{})]}),
  e.jsxs("a",{href:E,target:"_blank",rel:"noopener noreferrer",onClick:()=>w("use_cases",E),
   className:"group relative col-span-2 flex h-35 items-center justify-between overflow-hidden rounded-3xl bg-(--fg-1) [corner-shape:superellipse(1.4)]",
children:[e.jsxs("div",{
   className:"relative flex shrink-0 flex-col gap-4 pt-6 pl-5",
children:[e.jsx("div",{
   className:"flex items-end gap-3",children:ue.map(s=>e.jsx("img",{src:s.src,alt:"",
   className:"h-10.5 w-auto"},s.alt))}),
  e.jsx("div",{
   className:"flex flex-col gap-0.75 font-['IBM_Plex_Serif'] text-(--text-1) text-[12px] leading-[150%]",children:ge.map(s=>e.jsx("p",{children:s},s))}),
  e.jsx("div",{
   className:"pointer-events-none absolute inset-x-0 bottom-0 h-15 bg-linear-to-b from-transparent to-(--fg-1)"})]}),
  e.jsxs("div",{
   className:"mr-11.5 flex flex-col items-center justify-center",
children:[e.jsx("span",{
   className:"text-(--text-3)",children:e.jsx(q,{size:64})}),
  e.jsx("span",{
   className:"font-medium text-(--text-2) text-[12px] leading-3.5",children:"Learn more"})]}),
  e.jsx(b,{})]})]})]})}const be="data:image/svg+xml,%3csvg%20preserveAspectRatio='none'%20width='100%25'%20height='100%25'%20overflow='visible'%20style='display:%20block;'%20viewBox='0%200%20574.552%20353.008'%20fill='none'%20xmlns='http://www.w3.org/2000/svg'%3e%3cg%20id='Group%203'%3e%3cpath%20id='3%20(Stroke)'%20d='M501.769%20176.504C501.769%20128.766%20506.723%2086.133%20514.427%2055.9424C518.312%2040.7161%20522.658%2029.6217%20526.805%2022.7476C527.883%2020.9605%20528.792%2019.6986%20529.513%2018.8155C530.233%2019.6986%20531.143%2020.9605%20532.221%2022.7476C536.368%2029.6217%20540.713%2040.7161%20544.599%2055.9424C552.303%2086.133%20557.257%20128.766%20557.257%20176.504C557.257%20224.242%20552.303%20266.875%20544.599%20297.065C540.713%20312.292%20536.368%20323.386%20532.221%20330.26C531.143%20332.047%20530.233%20333.308%20529.513%20334.192C528.792%20333.308%20527.883%20332.047%20526.805%20330.26C522.658%20323.386%20518.312%20312.292%20514.427%20297.065C506.723%20266.875%20501.769%20224.242%20501.769%20176.504ZM484.477%20178.785C484.789%20275.215%20504.833%20353.008%20529.513%20353.008L529.804%20353.005C554.544%20352.391%20574.552%20273.604%20574.552%20176.504C574.552%2079.0235%20554.387%200%20529.513%200C504.638%201.16444e-05%20484.474%2079.0235%20484.474%20176.504L484.477%20178.785Z'%20fill='var(--fill-0,%20%23F892B8)'/%3e%3cpath%20id='1%20(Stroke)'%20d='M17.2212%20176.504C17.2214%2087.9752%2086.3648%2017.2316%20170.418%2017.2316C254.471%2017.2316%20323.615%2087.9752%20323.615%20176.504C323.615%20265.033%20254.471%20335.777%20170.418%20335.777V353.008L171.52%20353.005C265.132%20352.391%20340.836%20273.604%20340.836%20176.504C340.836%2079.0235%20264.537%200%20170.418%200L169.316%200.00350578C75.7038%200.617612%200.000176%2079.4043%205.6386e-05%20176.504L0.00356004%20177.645C0.596296%20274.601%2076.6663%20353.008%20170.418%20353.008V335.777C86.3647%20335.777%2017.2212%20265.033%2017.2212%20176.504Z'%20fill='var(--fill-0,%20%23009376)'/%3e%3cpath%20id='2%20(Stroke)'%20d='M292.35%20176.504C292.35%20129.818%20301.696%2088.4025%20316.014%2059.2981C330.881%2029.0772%20348.165%2017.2316%20361.936%2017.2316C375.706%2017.2317%20392.99%2029.0774%20407.857%2059.2981C422.175%2088.4025%20431.521%20129.818%20431.521%20176.504C431.521%20223.189%20422.175%20264.606%20407.857%20293.71C392.99%20323.931%20375.706%20335.777%20361.936%20335.777V353.008L362.497%20353.005C410.194%20352.391%20448.767%20273.604%20448.767%20176.504C448.767%2079.0236%20409.891%200.000247006%20361.936%200C313.98%200%20275.103%2079.0235%20275.103%20176.504L275.11%20178.785C275.711%20275.215%20314.354%20353.008%20361.936%20353.008V335.777C348.165%20335.777%20330.881%20323.931%20316.014%20293.71C301.696%20264.606%20292.35%20223.189%20292.35%20176.504Z'%20fill='var(--fill-0,%20%23BEDB00)'/%3e%3c/g%3e%3c/svg%3e";function Ce(){return e.jsxs("section",{
   className:"relative h-120 w-118 shrink-0 overflow-hidden rounded-4xl bg-(--bg-3) [corner-shape:superellipse(1.4)]",
children:[e.jsx("img",{src:be,alt:"","aria-hidden":!0,
   className:"pointer-events-none absolute -bottom-23 -left-12.5 h-88.25 w-143.75 max-w-none"}),
  e.jsxs("div",{
   className:"relative flex h-full flex-col",
children:[e.jsxs("header",{
   className:"flex flex-col gap-3 px-10 py-8",
children:[e.jsxs("h2",{
   className:"font-semibold text-(--text-1) text-[28px] leading-[1.15]",
children:["ego ",e.jsx("span",{
   className:"font-light italic",children:"(lite)"})," is ready to",e.jsx("br",{}),"work with your agents"]}),
  e.jsx("p",{
   className:"text-(--text-3) text-[16px] leading-4.75",children:"Your agent now runs browser tasks faster, reuses past successes, and uses fewer tokens."})]}),
  e.jsx("div",{
   className:"mt-auto p-2",children:e.jsx("div",{
   className:"flex flex-col rounded-3xl border border-(--bg-alpha-2) bg-(--fg-alpha-4) px-5 py-4 backdrop-blur-[20px] [corner-shape:superellipse(1.4)]",children:e.jsxs("ul",{
   className:"list-disc space-y-2.5 ps-6 text-(--text-3) text-[16px] leading-4.75",
children:[e.jsxs("li",{
children:["To start using ego,"," ",e.jsx("span",{
   className:"text-(--text-1)",children:"restart your agent."})]}),
  e.jsxs("li",{
children:["If ego ",e.jsx("span",{
   className:"italic",children:"(lite)"})," is unavailable or the skill is missing, manually install it by running `npx skills add citrolabs/ego-lite` in your agent."]})]})})})]})]})}const S="data:image/svg+xml,%3csvg%20height='1em'%20style='flex:none;line-height:1'%20viewBox='0%200%2024%2024'%20width='1em'%20xmlns='http://www.w3.org/2000/svg'%3e%3ctitle%3eClaude%3c/title%3e%3cpath%20d='M4.709%2015.955l4.72-2.647.08-.23-.08-.128H9.2l-.79-.048-2.698-.073-2.339-.097-2.266-.122-.571-.121L0%2011.784l.055-.352.48-.321.686.06%201.52.103%202.278.158%201.652.097%202.449.255h.389l.055-.157-.134-.098-.103-.097-2.358-1.596-2.552-1.688-1.336-.972-.724-.491-.364-.462-.158-1.008.656-.722.881.06.225.061.893.686%201.908%201.476%202.491%201.833.365.304.145-.103.019-.073-.164-.274-1.355-2.446-1.446-2.49-.644-1.032-.17-.619a2.97%202.97%200%2001-.104-.729L6.283.134%206.696%200l.996.134.42.364.62%201.414%201.002%202.229%201.555%203.03.456.898.243.832.091.255h.158V9.01l.128-1.706.237-2.095.23-2.695.08-.76.376-.91.747-.492.584.28.48.685-.067.444-.286%201.851-.559%202.903-.364%201.942h.212l.243-.242.985-1.306%201.652-2.064.73-.82.85-.904.547-.431h1.033l.76%201.129-.34%201.166-1.064%201.347-.881%201.142-1.264%201.7-.79%201.36.073.11.188-.02%202.856-.606%201.543-.28%201.841-.315.833.388.091.395-.328.807-1.969.486-2.309.462-3.439.813-.042.03.049.061%201.549.146.662.036h1.622l3.02.225.79.522.474.638-.079.485-1.215.62-1.64-.389-3.829-.91-1.312-.329h-.182v.11l1.093%201.068%202.006%201.81%202.509%202.33.127.578-.322.455-.34-.049-2.205-1.657-.851-.747-1.926-1.62h-.128v.17l.444.649%202.345%203.521.122%201.08-.17.353-.608.213-.668-.122-1.374-1.925-1.415-2.167-1.143-1.943-.14.08-.674%207.254-.316.37-.729.28-.607-.461-.322-.747.322-1.476.389-1.924.315-1.53.286-1.9.17-.632-.012-.042-.14.018-1.434%201.967-2.18%202.945-1.726%201.845-.414.164-.717-.37.067-.662.401-.589%202.388-3.036%201.44-1.882.93-1.086-.006-.158h-.055L4.132%2018.56l-1.13.146-.487-.456.061-.746.231-.243%201.908-1.312-.006.006z'%20fill='%23D97757'%20fill-rule='nonzero'%3e%3c/path%3e%3c/svg%3e",T="data:image/svg+xml,%3csvg%20height='1em'%20style='flex:none;line-height:1'%20viewBox='0%200%2024%2024'%20width='1em'%20xmlns='http://www.w3.org/2000/svg'%3e%3ctitle%3eCodex%3c/title%3e%3cpath%20d='M19.503%200H4.496A4.496%204.496%200%20000%204.496v15.007A4.496%204.496%200%20004.496%2024h15.007A4.496%204.496%200%200024%2019.503V4.496A4.496%204.496%200%200019.503%200z'%20fill='%23fff'%3e%3c/path%3e%3cpath%20d='M9.064%203.344a4.578%204.578%200%20012.285-.312c1%20.115%201.891.54%202.673%201.275.01.01.024.017.037.021a.09.09%200%2000.043%200%204.55%204.55%200%20013.046.275l.047.022.116.057a4.581%204.581%200%20012.188%202.399c.209.51.313%201.041.315%201.595a4.24%204.24%200%2001-.134%201.223.123.123%200%2000.03.115c.594.607.988%201.33%201.183%202.17.289%201.425-.007%202.71-.887%203.854l-.136.166a4.548%204.548%200%2001-2.201%201.388.123.123%200%2000-.081.076c-.191.551-.383%201.023-.74%201.494-.9%201.187-2.222%201.846-3.711%201.838-1.187-.006-2.239-.44-3.157-1.302a.107.107%200%2000-.105-.024c-.388.125-.78.143-1.204.138a4.441%204.441%200%2001-1.945-.466%204.544%204.544%200%2001-1.61-1.335c-.152-.202-.303-.392-.414-.617a5.81%205.81%200%2001-.37-.961%204.582%204.582%200%2001-.014-2.298.124.124%200%2000.006-.056.085.085%200%2000-.027-.048%204.467%204.467%200%2001-1.034-1.651%203.896%203.896%200%2001-.251-1.192%205.189%205.189%200%2001.141-1.6c.337-1.112.982-1.985%201.933-2.618.212-.141.413-.251.601-.33.215-.089.43-.164.646-.227a.098.098%200%2000.065-.066%204.51%204.51%200%2001.829-1.615%204.535%204.535%200%20011.837-1.388zm3.482%2010.565a.637.637%200%20000%201.272h3.636a.637.637%200%20100-1.272h-3.636zM8.462%209.23a.637.637%200%2000-1.106.631l1.272%202.224-1.266%202.136a.636.636%200%20101.095.649l1.454-2.455a.636.636%200%2000.005-.64L8.462%209.23z'%20fill='url(%23lobe-icons-codex-_R_0_)'%3e%3c/path%3e%3cdefs%3e%3clinearGradient%20gradientUnits='userSpaceOnUse'%20id='lobe-icons-codex-_R_0_'%20x1='12'%20x2='12'%20y1='3'%20y2='21'%3e%3cstop%20stop-color='%23B1A7FF'%3e%3c/stop%3e%3cstop%20offset='.5'%20stop-color='%237A9DFF'%3e%3c/stop%3e%3cstop%20offset='1'%20stop-color='%233941FF'%3e%3c/stop%3e%3c/linearGradient%3e%3c/defs%3e%3c/svg%3e",O=""+new URL("../assets/cursor-Ba7_PyGL.svg",import.meta.url).href,D=""+new URL("../assets/hermes-CylAUprn.svg",import.meta.url).href,ve="data:image/svg+xml,%3csvg%20width='1200'%20height='1200'%20viewBox='0%200%201200%201200'%20fill='none'%20xmlns='http://www.w3.org/2000/svg'%3e%3crect%20width='1200'%20height='1200'%20rx='260'%20fill='%239046FF'/%3e%3cmask%20id='mask0_1106_4856'%20style='mask-type:luminance'%20maskUnits='userSpaceOnUse'%20x='272'%20y='202'%20width='655'%20height='796'%3e%3cpath%20d='M926.578%20202.793H272.637V997.857H926.578V202.793Z'%20fill='white'/%3e%3c/mask%3e%3cg%20mask='url(%23mask0_1106_4856)'%3e%3cpath%20d='M398.554%20818.914C316.315%201001.03%20491.477%201046.74%20620.672%20940.156C658.687%201059.66%20801.052%20970.473%20852.234%20877.795C964.787%20673.567%20919.318%20465.357%20907.64%20422.374C827.637%20129.443%20427.623%20128.946%20358.8%20423.865C342.651%20475.544%20342.402%20534.18%20333.458%20595.051C328.986%20625.86%20325.507%20645.488%20313.83%20677.785C306.873%20696.424%20297.68%20712.819%20282.773%20740.645C259.915%20783.881%20269.604%20867.113%20387.87%20823.883L399.051%20818.914H398.554Z'%20fill='white'/%3e%3cpath%20d='M636.123%20549.353C603.328%20549.353%20598.359%20510.097%20598.359%20486.742C598.359%20465.623%20602.086%20448.977%20609.293%20438.293C615.504%20428.852%20624.697%20424.131%20636.123%20424.131C647.555%20424.131%20657.492%20428.852%20664.447%20438.541C672.398%20449.474%20676.623%20466.12%20676.623%20486.742C676.623%20525.998%20661.471%20549.353%20636.375%20549.353H636.123Z'%20fill='black'/%3e%3cpath%20d='M771.24%20549.353C738.445%20549.353%20733.477%20510.097%20733.477%20486.742C733.477%20465.623%20737.203%20448.977%20744.41%20438.293C750.621%20428.852%20759.814%20424.131%20771.24%20424.131C782.672%20424.131%20792.609%20428.852%20799.564%20438.541C807.516%20449.474%20811.74%20466.12%20811.74%20486.742C811.74%20525.998%20796.588%20549.353%20771.492%20549.353H771.24Z'%20fill='black'/%3e%3c/g%3e%3c/svg%3e",je="data:image/svg+xml,%3csvg%20viewBox='0%200%20120%20120'%20fill='none'%20xmlns='http://www.w3.org/2000/svg'%3e%3cdefs%3e%3clinearGradient%20id='lobster-gradient'%20x1='0%25'%20y1='0%25'%20x2='100%25'%20y2='100%25'%3e%3cstop%20offset='0%25'%20stop-color='%23ff4d4d'/%3e%3cstop%20offset='100%25'%20stop-color='%23991b1b'/%3e%3c/linearGradient%3e%3c/defs%3e%3c!--%20Body%20--%3e%3cpath%20d='M60%2010%20C30%2010%2015%2035%2015%2055%20C15%2075%2030%2095%2045%20100%20L45%20110%20L55%20110%20L55%20100%20C55%20100%2060%20102%2065%20100%20L65%20110%20L75%20110%20L75%20100%20C90%2095%20105%2075%20105%2055%20C105%2035%2090%2010%2060%2010Z'%20fill='url(%23lobster-gradient)'/%3e%3c!--%20Left%20Claw%20--%3e%3cpath%20d='M20%2045%20C5%2040%200%2050%205%2060%20C10%2070%2020%2065%2025%2055%20C28%2048%2025%2045%2020%2045Z'%20fill='url(%23lobster-gradient)'/%3e%3c!--%20Right%20Claw%20--%3e%3cpath%20d='M100%2045%20C115%2040%20120%2050%20115%2060%20C110%2070%20100%2065%2095%2055%20C92%2048%2095%2045%20100%2045Z'%20fill='url(%23lobster-gradient)'/%3e%3c!--%20Antenna%20--%3e%3cpath%20d='M45%2015%20Q35%205%2030%208'%20stroke='%23ff4d4d'%20stroke-width='3'%20stroke-linecap='round'/%3e%3cpath%20d='M75%2015%20Q85%205%2090%208'%20stroke='%23ff4d4d'%20stroke-width='3'%20stroke-linecap='round'/%3e%3c!--%20Eyes%20--%3e%3ccircle%20cx='45'%20cy='35'%20r='6'%20fill='%23050810'/%3e%3ccircle%20cx='75'%20cy='35'%20r='6'%20fill='%23050810'/%3e%3ccircle%20cx='46'%20cy='34'%20r='2.5'%20fill='%2300e5cc'/%3e%3ccircle%20cx='76'%20cy='34'%20r='2.5'%20fill='%2300e5cc'/%3e%3c/svg%3e",z="data:image/svg+xml,%3csvg%20xmlns='http://www.w3.org/2000/svg'%20version='1.1'%20xmlns:xlink='http://www.w3.org/1999/xlink'%20width='512'%20height='512'%3e%3csvg%20width='512'%20height='512'%20viewBox='0%200%20512%20512'%20fill='none'%20xmlns='http://www.w3.org/2000/svg'%3e%3crect%20width='512'%20height='512'%20fill='%23131010'%3e%3c/rect%3e%3cpath%20d='M320%20224V352H192V224H320Z'%20fill='%235A5858'%3e%3c/path%3e%3cpath%20fill-rule='evenodd'%20clip-rule='evenodd'%20d='M384%20416H128V96H384V416ZM320%20160H192V352H320V160Z'%20fill='white'%3e%3c/path%3e%3c/svg%3e%3cstyle%3e@media%20(prefers-color-scheme:%20light)%20{%20:root%20{%20filter:%20none;%20}%20}%20@media%20(prefers-color-scheme:%20dark)%20{%20:root%20{%20filter:%20none;%20}%20}%20%3c/style%3e%3c/svg%3e",Ne="data:image/png;base64,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";function ye({
   className:s}){return e.jsxs("div",{
   className:f("dark flex h-90.75 w-166.25 flex-col overflow-hidden bg-(--bg-1) pt-5 pl-3.25 font-['IBM_Plex_Mono'] text-(--text-2) text-[14px] leading-[1.15]",s),
children:[e.jsxs("div",{
   className:"flex items-center gap-4.5",
children:[e.jsx("img",{src:Ne,alt:"","aria-hidden":!0,
   className:"h-10.5 w-17.25 shrink-0"}),
  e.jsxs("div",{
   className:"flex flex-col gap-px",
children:[e.jsxs("p",{
   className:"flex gap-0.75 whitespace-nowrap",
children:[e.jsx("span",{
   className:"font-bold",children:"Claude Code"}),
  e.jsx("span",{
   className:"text-(--text-3)",children:"v2.1.159"})]}),
  e.jsx("span",{
   className:"text-(--text-3)",children:"Opus 4.8"})]})]}),
  e.jsx("div",{
   className:"mt-5.5 self-start bg-(--bg-alpha-4) p-0.5",children:e.jsxs("p",{
   className:"whitespace-nowrap font-medium",
children:[e.jsx("span",{
   className:"text-(--text-5)",children:"❯ "}),
  e.jsx("span",{children:"/ego-browser summarize OpenAI & Anthropic blogs"})]})}),
  e.jsxs("div",{
   className:"mt-5 flex items-start gap-1.5 font-medium",
children:[e.jsx("span",{"aria-hidden":!0,
   className:"mt-1.25 size-1.5 shrink-0 rounded-full bg-(--text-2)"}),
  e.jsxs("div",{
   className:"whitespace-nowrap",
children:[e.jsxs("p",{
children:["I'll use ego-browser to explore OpenAI's"," ",e.jsx("span",{
   className:"text-(--text-3)",children:"main"})," ",e.jsx("span",{
   className:"text-(--text-4)",children:"page "}),
  e.jsx("span",{
   className:"text-(--text-5)",children:"and discover"})]}),
  e.jsx("p",{
   className:"text-(--text-5)",children:`today's interesting articles — featured article, "Did you know", "In`}),
  e.jsx("p",{
   className:"text-(--text-5)",children:'the news", "On this day", etc.'})]})]}),
  e.jsxs("div",{
   className:"mt-4 flex flex-col gap-4",
children:[e.jsx("div",{
   className:"h-4.5 w-131 bg-(--bg-alpha-1)"}),
  e.jsx("div",{
   className:"h-4.5 w-100.5 bg-(--bg-alpha-1)"})]})]})}const y="/ego-browser",G="OpenAI & Anthropic blogs, summarize latest noteworthy updates",C=`${y} ${G}`,Ae=2e3,I=[{name:"Claude Code",icon:S},{name:"Codex",icon:T},{name:"Cursor",icon:O},{name:"Hermes",icon:D},{name:"Kiro",icon:ve},{name:"OpenClaw",icon:je},{name:"OpenCode",icon:z}],v=[{name:"Codex",icon:T,promptUrlPrefix:"codex://new?prompt=",appUrl:null},{name:"Claude Code",icon:S,promptUrlPrefix:"claude://code/new?q=",appUrl:null},{name:"OpenCode",icon:z,promptUrlPrefix:null,appUrl:"opencode://open"},{name:"Cursor",icon:O,promptUrlPrefix:"cursor://anysphere.cursor-deeplink/prompt?text=",appUrl:null},{name:"Hermes",icon:D,promptUrlPrefix:null,appUrl:"hermes://open"}],ke="Codex",Ee=1500;function Me(){const[s,a]=l.useState(0),[n,m]=l.useState(ke),[p,d]=l.useState(!1),[,x]=Q(),i=l.useRef(null),r=v.find(t=>t.name===n)??v[0];l.useEffect(()=>{const t=setInterval(()=>{a(o=>(o+1)%I.length)},Ee);return()=>clearInterval(t)},[]),l.useEffect(()=>()=>{i.current&&clearTimeout(i.current)},[]);const c=async()=>{await x(C),de(y,C)},u=async()=>{await c(),d(!0),i.current&&clearTimeout(i.current),i.current=setTimeout(()=>{d(!1)},Ae)},h=async()=>{await c();const t=r.promptUrlPrefix?`${r.promptUrlPrefix}${encodeURIComponent(C)}`:r.appUrl;t&&(window.location.href=t)};return e.jsxs("section",{
   className:"relative flex h-120 w-118 shrink-0 flex-col overflow-hidden rounded-4xl bg-(--bg-3) [corner-shape:superellipse(1.4)]",
children:[e.jsxs("header",{
   className:"flex flex-col gap-3 px-10 pt-8 pb-6",
children:[e.jsxs("h2",{
   className:"flex flex-wrap items-center font-semibold text-(--text-1) text-[28px] leading-[1.15]",
children:[e.jsxs("span",{
children:["Try ego ",e.jsx("span",{
   className:"font-light italic",children:"(lite)"})," with your"]}),
  e.jsx("span",{
   className:"relative mx-1.5 size-5 shrink-0",children:I.map((t,o)=>e.jsx("img",{src:t.icon,alt:o===s?t.name:"","aria-hidden":o!==s,
   className:f("absolute inset-0 size-full",o===s?"opacity-100":"opacity-0")},t.name))}),
  e.jsx("span",{children:"agent"})]}),
  e.jsx("p",{
   className:"text-(--text-3) text-[16px] leading-4.75",children:"Type /ego-browser followed by your task in the agent, or specify using ego browser in your prompt."})]}),
  e.jsxs("div",{
   className:"flex flex-col px-8",
children:[e.jsxs("div",{
   className:"relative flex w-full items-center justify-between gap-3 rounded-full bg-(--fg-1) py-2 pr-3 pl-4 [corner-shape:superellipse(1.4)]",
children:[e.jsx("button",{type:"button",onClick:h,"aria-label":`Open prompt in ${r.name}`,
   className:"absolute inset-0 rounded-full [corner-shape:superellipse(1.4)] hover:bg-(--fg-3)"}),
  e.jsxs("div",{
   className:"pointer-events-none relative z-10 flex items-center gap-3",
children:[e.jsx("span",{
   className:"font-medium text-(--text-1) text-[16px] leading-4.75",children:"Open in"}),
  e.jsx("span",{
   className:"pointer-events-auto",children:e.jsxs(K,{
children:[e.jsx(Z,{asChild:!0,children:e.jsxs("button",{type:"button",
   className:"flex items-center gap-1.5 rounded-full bg-(--bg-alpha-2) py-2 pr-1.5 pl-2 outline-none transition-colors [corner-shape:superellipse(1.4)] hover:bg-(--bg-alpha-3)",
children:[e.jsx("img",{src:r.icon,alt:"",
   className:"size-5"}),
  e.jsx("span",{
   className:"font-medium text-(--text-1) text-[14px] leading-4.25",children:r.name}),
  e.jsx("span",{
   className:"text-(--text-3)",children:e.jsx(W,{size:20})})]})}),
  e.jsx(X,{align:"start",
   className:"w-50",children:e.jsx(J,{value:n,onValueChange:m,children:v.map(t=>e.jsx(Y,{value:t.name,icon:e.jsx("img",{src:t.icon,alt:"",
   className:"size-5"}),children:t.name},t.name))})})]})}),r.promptUrlPrefix===null?e.jsxs("span",{
   className:"shrink-0 whitespace-nowrap font-medium text-[16px] leading-4.75",
children:[e.jsx("span",{
   className:"text-(--text-1)",children:"and Paste"})," ",e.jsx("span",{
   className:"text-(--text-4)",children:"⌘V"})]}):null]}),
  e.jsx("span",{
   className:"pointer-events-none relative z-10 flex size-8 shrink-0 items-center justify-center p-1.5 text-(--text-1)",children:e.jsx("span",{
   className:"inline-flex",children:e.jsx($,{size:32})})})]}),
  e.jsxs("div",{
   className:"flex flex-col pt-3",
children:[e.jsxs("div",{
   className:"flex items-center justify-center gap-1.5 px-2",
children:[e.jsx("span",{
   className:"h-0 flex-1 border-(--fg-5) border-t border-dashed"}),
  e.jsx("span",{
   className:"shrink-0 font-medium text-(--text-4) text-[12px] leading-3.5",children:"or copy and paste in your terminal"}),
  e.jsx("span",{
   className:"h-0 flex-1 border-(--fg-5) border-t border-dashed"})]}),
  e.jsxs("div",{
   className:"flex items-center justify-center gap-2.5 p-2 pb-0",
children:[e.jsxs("p",{
   className:"min-w-0 flex-1 font-medium text-(--text-3) text-[12px] leading-3.5",
children:[e.jsxs("span",{
children:[y," "]}),
  e.jsx("span",{children:G})]}),
  e.jsx("button",{type:"button",onClick:u,"aria-label":p?"Copied":"Copy prompt",
   className:f("flex shrink-0 items-center rounded-lg p-1.5 text-(--text-1) transition-colors [corner-shape:superellipse(1.4)]",p?"gap-1.5 bg-(--bg-alpha-4) pr-2":"hover:bg-(--bg-alpha-4)"),children:p?e.jsx(ee,{size:20}):e.jsx(se,{size:20})})]})]})]}),
  e.jsx("div",{
   className:"mt-auto flex h-50 items-start justify-center overflow-hidden p-2 pt-0",children:e.jsx("div",{
   className:"h-57 w-104.5 shrink-0 overflow-hidden rounded-xl [corner-shape:superellipse(1.4)]",children:e.jsx(ye,{
   className:"origin-top-left scale-[0.72]"})})})]})}function Re(){return e.jsxs("div",{
   className:"flex flex-col items-center gap-12 pt-12",
children:[e.jsx("span",{role:"img","aria-label":"ego",
   className:"inline-flex text-(--fg-4) [&_g]:opacity-100",children:e.jsx(te,{width:78,height:48})}),
  e.jsxs("div",{
   className:"flex w-full flex-col items-center gap-5 text-center",
children:[e.jsxs("h1",{
   className:"font-black text-(--text-2) text-[64px] leading-[0.85]",
children:["Welcome",e.jsx("br",{}),"aboard!"]}),
  e.jsx("p",{
   className:"max-w-90 text-(--text-3) text-base",children:"Follow the guides below to get started"})]})]})}const j=100,Ie=30,L=300;function P(s,a,n){return Math.min(Math.max(s,a),n)}function Le(){const s=l.useRef(null),a=l.useRef(null),n=l.useRef(null),[m,p]=l.useState({gap:j,spacer:L});return l.useLayoutEffect(()=>{const d=s.current,x=a.current,i=n.current;if(!(d&&x&&i))return;const r=()=>{const u=x.getBoundingClientRect().height+i.getBoundingClientRect().height,h=d.clientHeight-u,t=P(h,Ie,j),o=P(h-j,0,L);p(g=>g.gap===t&&g.spacer===o?g:{gap:t,spacer:o})};if(r(),typeof ResizeObserver>"u")return;const c=new ResizeObserver(r);return c.observe(d),c.observe(x),c.observe(i),()=>c.disconnect()},[]),
  e.jsxs(e.Fragment,{
children:[e.jsx("style",{children:`
				body { background: var(--bg-1); }

				.welcome-aboard-cards-viewport {
					container-type: inline-size;
				}

				.welcome-aboard-cards-row {
					display: flex;
					flex-wrap: wrap;
					align-items: center;
					justify-content: center;
					gap: 12px;
				}

				@media (min-width: 1264px) {
					.welcome-aboard-cards-stage {
						--welcome-aboard-cards-scale: min(1, calc(100cqw / 1440px));
						position: relative;
						width: min(100%, 1440px);
						max-width: none;
						aspect-ratio: 1440 / 480;
					}

					.welcome-aboard-cards-row {
						position: absolute;
						top: 0;
						left: 50%;
						width: 1440px;
						flex-wrap: nowrap;
						transform: translateX(-50%) scale(var(--welcome-aboard-cards-scale));
						transform-origin: top center;
					}
				}

				@container (min-width: 1440px) {
					.welcome-aboard-cards-stage {
						position: static;
						width: 100%;
						max-width: 1440px;
						aspect-ratio: auto;
					}

					.welcome-aboard-cards-row {
						position: static;
						width: 100%;
						transform: none;
					}
				}
			`}),
  e.jsx("main",{ref:s,
   className:"flex h-dvh w-full flex-col overflow-y-auto px-4 [scrollbar-gutter:stable_both-edges]",children:e.jsxs("div",{
   className:"mx-auto my-auto flex w-full max-w-360 shrink-0 flex-col items-center",
children:[e.jsx("div",{ref:a,
   className:"shrink-0",children:e.jsx(Re,{})}),
  e.jsx("div",{"aria-hidden":!0,
   className:"w-full shrink-0",style:{height:m.gap}}),
  e.jsx("div",{ref:n,
   className:"welcome-aboard-cards-viewport flex w-full shrink-0 justify-center",children:e.jsx("div",{
   className:"welcome-aboard-cards-stage w-full max-w-360",children:e.jsxs("div",{
   className:"welcome-aboard-cards-row",
children:[e.jsx(Ce,{}),
  e.jsx(Me,{}),
  e.jsx(we,{})]})})}),
  e.jsx("div",{"aria-hidden":!0,
   className:"w-full shrink-0",style:{height:m.spacer}})]})})]})}const H=document.getElementById("app");if(!H)throw new Error("Root element #app not found");ce();V.createRoot(H).render(e.jsx(l.StrictMode,{children:e.jsxs(F,{attribute:"class",defaultTheme:"system",disableTransitionOnChange:!0,enableSystem:!0,storageKey:"welcome-aboard-theme",
children:[e.jsx(Le,{}),
  e.jsx(ae,{position:"top-center"})]})}));
