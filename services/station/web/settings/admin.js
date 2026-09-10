import {StationAPI, ApiError} from './api.js';
const $ = id => document.getElementById(id), api = new StationAPI();
let current = null, draft = null, caps = null, etag = '', changed = false, timer = null, busy = false;
const clone = value => JSON.parse(JSON.stringify(value));
function message(text, error = false) { $('message').textContent = text; $('message').dataset.error = String(error); }
function dirty() { changed = true; $('dirty').textContent = 'Есть несохранённые изменения'; $('confirm-reprocess').checked = false; $('json-settings').value = JSON.stringify(draft, null, 2); }
function element(tag, text, parent, className = '') { const node = document.createElement(tag); if (text) node.textContent = text; if (className) node.className = className; if (parent) parent.append(node); return node; }
function field(parent, title, value, apply, type = 'text', options = {}) {
  const label = element('label', title, parent, type === 'checkbox' ? 'check' : (options.wide ? 'wide' : ''));
  const input = element(type === 'select' ? 'select' : 'input', '', label);
  if (type === 'select') for (const item of options.values || []) { const option = element('option', Array.isArray(item) ? item[1] : item, input); option.value = Array.isArray(item) ? item[0] : item; }
  else input.type = type;
  if (type === 'checkbox') input.checked = !!value; else input.value = value == null ? '' : value;
  if (options.range) { input.min = options.range[0]; input.max = options.range[1]; input.step = 1; }
  if (options.hint) element('span', options.hint, label, 'hint');
  input.addEventListener('input', () => input.setCustomValidity(''));
  input.addEventListener('change', () => {
    if (!input.checkValidity()) { input.reportValidity(); return; }
    apply(type === 'checkbox' ? input.checked : type === 'number' ? Number(input.value) : input.value); dirty();
  });
  return input;
}
const DISPLAY_LABELS = {
  windowHours:'Глубина истории, ч',pollSeconds:'Обновление каталога, с',minimumDisplaySeconds:'Минимальное время кадра, с',
  displaySeconds:'Обычный кадр, с',complexDisplaySeconds:'Кадр с легендой, с',latestSeconds:'Самый свежий кадр, с',preloadSeconds:'Предзагрузка за, с',
  transitionMilliseconds:'Переход между кадрами, мс',portraitTourTransitionMilliseconds:'Перемещение вертикального снимка, мс',
  sourceDelayHours:'Предупреждение о задержке, ч',imageTimeoutSeconds:'Ожидание загрузки снимка, с',
  portraitTourEnabled:'Обзор вертикального снимка',performanceMode:'Режим слабого компьютера',ambient:'Цветной фон из снимка',ambientOpacityPercent:'Выраженность фона, %',dailyReload:'Ежедневная перезагрузка в UTC-окне'
};
const STATION_LABELS = {poll_seconds:'Проверка входных папок, с',settle_seconds:'Ожидание завершения записи, с',native_threads:'Потоки декодирования',timeout_seconds:'Лимит задания, с',max_attempts:'Число попыток',min_free_mb:'Резерв диска, МиБ',max_input_mb:'Максимум входа, МиБ',max_items:'Лимит каталога станции',max_image_pixels:'Лимит пикселей изображения',max_log_mb:'Лимит журнала, МиБ'};
function sourceEditor() {
  const parent = $('sources'); parent.replaceChildren();
  draft.station.sources.forEach((source, index) => {
    const box = element('fieldset', '', parent); element('legend', `Источник ${index + 1}`, box); const fields = element('div', '', box, 'fields');
    for (const [key, title] of [['id','Идентификатор'],['path','Входная папка'],['satellite','Спутник'],['instrument','Прибор']]) field(fields,title,source[key],value=>{source[key]=value;});
    field(fields,'Тип данных',source.kind,value=>{
      source.kind=value;
      if(value !== 'pipeline') { delete source.pipeline;delete source.input_level;delete source.options; }
      else {source.pipeline=caps.pipeline_ids[0] || '';source.input_level='baseband';source.options=[];}
      sourceEditor();
    },'select',{values:[['image','Готовые PNG/JPEG + паспорт'],['product','Каталог product.cbor'],['pipeline','Радиоданные для конвейера']]});
    field(fields,'Включён',source.enabled !== false,value=>{source.enabled=value;},'checkbox');
    field(fields,'Требовать маркер .ready',source.require_ready !== false,value=>{source.require_ready=value;},'checkbox');
    field(fields,'Маски файлов через запятую',(source.patterns || []).join(', '),value=>{source.patterns=value.split(',').map(s=>s.trim()).filter(Boolean);});
    if(source.kind === 'pipeline') {
      field(fields,'Конвейер',source.pipeline,value=>{source.pipeline=value;},'select',{values:caps.pipeline_ids});
      field(fields,'Уровень входа',source.input_level,value=>{source.input_level=value;});
      const input = field(fields,'Параметры конвейера — JSON-массив',JSON.stringify(source.options || []),value=>{
        try {const options=JSON.parse(value);if(!Array.isArray(options))throw Error();source.options=options;input.setCustomValidity('');}
        catch (_) {input.setCustomValidity('Нужен JSON-массив строк');input.reportValidity();}
      });
    }
    const actions=element('div','',box,'source-actions'),remove=element('button','Удалить из конфигурации',actions,'secondary');remove.type='button';
    remove.onclick=()=>{draft.station.sources.splice(index,1);dirty();sourceEditor();};
  });
}
function presetValue(create = false) {
  const instrument=$('instrument').value,preset=$('preset').value;
  if (!instrument || !preset) return null;
  const processing=draft.processing;
  if (create) {processing.viewer ||= {};processing.viewer.instruments ||= {};processing.viewer.instruments[instrument] ||= {};processing.viewer.instruments[instrument].rgb_composites ||= {};processing.viewer.instruments[instrument].rgb_composites[preset] ||= {};}
  return processing.viewer?.instruments?.[instrument]?.rgb_composites?.[preset] || null;
}
function presetEditor() {
  const parent=$('preset-fields');parent.replaceChildren();const value=presetValue() || {};
  const available=Boolean($('instrument').value && $('preset').value);$('apply-preset-json').disabled=!available;$('preset-presentation').disabled=!available;
  if(!available){$('preset-presentation').value='';element('p','Пресеты в установленном движке не обнаружены.',parent);return;}
  field(parent,'Автогенерация',value.autogen === undefined ? 'inherit' : String(value.autogen),selected=>{const v=presetValue(true);if(selected==='inherit')delete v.autogen;else v.autogen=selected==='true';},'select',{values:[['inherit','Из движка'],['true','Включена'],['false','Выключена']]});
  field(parent,'Выражение каналов',value.equation || '',selected=>{const v=presetValue(true);if(selected.trim())v.equation=selected.trim();else delete v.equation;},'text');
  $('preset-presentation').value=value.presentation === undefined ? '' : JSON.stringify(value.presentation,null,2);
}
function render() {
  for (const id of ['display-fields','station-fields','presentation-fields','board-fields']) $(id).replaceChildren();
  draft.board.display={...clone(caps.display_defaults),...draft.board.display};const display=draft.board.display;
  for(const [key,title] of Object.entries(DISPLAY_LABELS)) field($('display-fields'),title,display[key],value=>{display[key]=value;},typeof display[key]==='boolean'?'checkbox':'number',{range:caps.display_ranges[key]});
  field($('display-fields'),'Файл для показа',display.imageMode,value=>{display.imageMode=value;},'select',{values:[['original','Оригинал бэкенда — без уменьшения'],['preview','Превью бэкенда — для слабого экрана']]});
  field($('display-fields'),'UTC-окно перезагрузки: начало, конец',display.reloadHoursUTC.join(', '),value=>{display.reloadHoursUTC=value.split(',').map(s=>Number(s.trim()));});
  for(const [key,title] of [['productPriority','Приоритет продуктов через запятую'],['complexProducts','Сложные продукты через запятую']])field($('display-fields'),title,display[key].join(', '),value=>{display[key]=value.split(',').map(s=>s.trim()).filter(Boolean);},'text',{wide:true});
  field($('station-fields'),'Название станции',draft.station.title,value=>{draft.station.title=value;});
  for(const [key,title] of Object.entries(STATION_LABELS)) if(key in draft.station)field($('station-fields'),title,draft.station[key],value=>{draft.station[key]=value;},'number',{range:caps.station_ranges[key]});
  $('input-roots').textContent='Разрешённые корни входных папок: '+caps.input_roots.join(', ');sourceEditor();
  draft.processing.satdump_general ||= {};const general=draft.processing.satdump_general;
  const presentation=typeof general.presentation==='object' && general.presentation ? general.presentation : {};
  function writePresentation(key,value){general.presentation={...presentation,[key]:value};Object.assign(presentation,general.presentation);}
  field($('presentation-fields'),'Формировать оформление',general.presentation_enabled?.value !== false,value=>{general.presentation_enabled={value};},'checkbox');
  field($('presentation-fields'),'Минимальный макет',presentation.save_minimal !== false,value=>writePresentation('save_minimal',value),'checkbox');
  field($('presentation-fields'),'Полный макет',presentation.save_editorial ?? presentation.save_presentation ?? true,value=>{delete presentation.save_presentation;writePresentation('save_editorial',value);},'checkbox');
  field($('presentation-fields'),'Ориентировать на север',presentation.north_up !== false,value=>writePresentation('north_up',value),'checkbox');
  field($('presentation-fields'),'Ориентация',presentation.orientation_mode || 'auto',value=>writePresentation('orientation_mode',value),'select',{values:[['auto','Автоматически'],['keep','Сохранить исходную'],['flip_vertical','Отразить по вертикали'],['flip_horizontal','Отразить по горизонтали'],['rotate_180','Повернуть на 180°']]});
  field($('presentation-fields'),'Показывать подпись организации',general.presentation_show_branding?.value !== false,value=>{general.presentation_show_branding={value};},'checkbox');
  const instrument=$('instrument');instrument.replaceChildren();for(const name of Object.keys(caps.instruments)){const opt=element('option',name,instrument);opt.value=name;}
  function instrumentsChanged(){const select=$('preset');select.replaceChildren();for(const name of caps.instruments[instrument.value] || []){const opt=element('option',name,select);opt.value=name;}presetEditor();}
  instrument.onchange=instrumentsChanged;$('preset').onchange=presetEditor;instrumentsChanged();
  for(const [key,title] of [['hidden_sources','Скрытые источники'],['hidden_instruments','Скрытые приборы'],['hidden_products','Скрытые продукты']])field($('board-fields'),`${title}, через точку с запятой`,(draft.board[key]||[]).join('; '),value=>{draft.board[key]=value.split(';').map(s=>s.trim()).filter(Boolean);},'text',{hint:'Разделитель — точка с запятой; имена должны совпадать с каталогом.'});
  field($('board-fields'),'Лимит кадров BOARD',draft.board.max_items || 1000,value=>{draft.board.max_items=value;},'number',{range:[1,10000]});
  for(const [key,title] of [['editorial','Полный макет'],['minimal','Минимальный макет'],['external','Внешние снимки']])field($('board-fields'),title,(draft.board.layouts||[]).includes(key),value=>{const set=new Set(draft.board.layouts||[]);value?set.add(key):set.delete(key);draft.board.layouts=[...set];},'checkbox');
  $('json-settings').value=JSON.stringify(draft,null,2);$('revision').textContent='Ревизия: '+current.revision;
}
async function status() {
  if(!api.token)return;
  try {const {data}=await api.status();$('applied').textContent=!data.worker_alive?'Обработчик не работает или нет свежего статуса':data.configuration_error?'Обработчик отклонил конфигурацию':data.applied?'Применено обработчиком':'Ожидает применения между заданиями';}
  catch(_){$('applied').textContent='Статус обработчика недоступен';}
  clearTimeout(timer);if(api.token)timer=setTimeout(status,5000);
}
async function load() {
  const [config,capabilities]=await Promise.all([api.config(),api.capabilities()]);current=config.data;etag=config.etag;caps=capabilities.data;draft=clone(current.settings);changed=false;
  $('login').hidden=true;$('settings').hidden=false;$('logout').hidden=false;$('dirty').textContent='Нет изменений';$('reprocess-panel').hidden=true;render();status();
}
async function run(action) {
  if(busy)return;busy=true;
  const controls=[...document.querySelectorAll('button,input,select,textarea')].map(node=>[node,node.disabled]);
  controls.forEach(([node])=>{node.disabled=true;});
  try{await action();}catch(error){
    if(error instanceof ApiError && error.status===412) message('Конфигурация изменена в другой вкладке. Черновик сохранён здесь; скопируйте его перед загрузкой актуальной ревизии.',true);
    else if(error instanceof ApiError && error.status===409){$('reprocess-panel').hidden=false;message('Подтвердите возможную повторную обработку и повторите сохранение.',true);}
    else if(error instanceof ApiError && error.status===401)message('Токен отсутствует, отозван или неверен. Выполните вход заново.',true);
    else message(error.name==='AbortError'?'Сервер не ответил. Изменения не подтверждены; проверьте текущую ревизию.':error.message,true);
  }finally{busy=false;controls.forEach(([node,disabled])=>{node.disabled=disabled;});}
}
$('login').onsubmit=event=>{event.preventDefault();run(async()=>{api.token=$('token').value.trim();$('token').value='';await load();message('Настройки загружены. Изменения пока сохраняются только в черновике.');});};
$('logout').onclick=()=>{if(changed&&!confirm('Отменить черновик и завершить сеанс?'))return;changed=false;api.token='';draft=null;current=null;clearTimeout(timer);$('settings').hidden=true;$('login').hidden=false;$('logout').hidden=true;$('json-settings').value='';message('Сеанс управления завершён.');};
$('reload').onclick=()=>{if(changed&&!confirm('Отменить несохранённые изменения и загрузить текущую конфигурацию?'))return;run(async()=>{await load();message('Загружена актуальная конфигурация.');});};
$('validate').onclick=()=>run(async()=>{const {data}=await api.validate(draft);$('reprocess-panel').hidden=!data.reprocessing_required;message(data.reprocessing_required?'Проверка пройдена. Потребуется подтверждение повторной обработки.':'Проверка пройдена. Повторная обработка не требуется.');});
$('settings').onsubmit=event=>{event.preventDefault();if(!$('settings').reportValidity())return;run(async()=>{const validation=await api.validate(draft);if(validation.data.reprocessing_required&&!$('confirm-reprocess').checked){$('reprocess-panel').hidden=false;message('Для изменения источников или рецепта подтвердите повторную обработку.');return;}const result=await api.save(draft,etag,$('confirm-reprocess').checked);current=result.data;etag=result.etag;draft=clone(current.settings);changed=false;$('dirty').textContent='Нет изменений';$('reprocess-panel').hidden=true;render();message(result.status===202?'Ревизия сохранена. Применение обработчиком проверяется отдельно.':'Конфигурация не изменилась.');status();});};
$('add-source').onclick=()=>{let n=1;while(draft.station.sources.some(s=>s.id===`source-${n}`))n++;draft.station.sources.push({id:`source-${n}`,kind:'image',path:'',enabled:false,require_ready:true,patterns:['*.png','*.jpg','*.jpeg']});dirty();sourceEditor();};
$('show-json').onclick=()=>{$('json-settings').value=JSON.stringify(draft,null,2);};
$('use-json').onclick=()=>run(async()=>{const candidate=JSON.parse($('json-settings').value);await api.validate(candidate);draft=candidate;dirty();render();message('JSON проверен и перенесён в черновик. Нажмите «Сохранить» для записи.');});
$('apply-preset-json').onclick=()=>run(async()=>{const value=$('preset-presentation').value.trim();const before=clone(draft);try{const target=presetValue(true);if(!target)throw Error('Нет доступного пресета');if(value)target.presentation=JSON.parse(value);else delete target.presentation;await api.validate(draft);dirty();message('Оформление пресета проверено и внесено в черновик.');}catch(error){draft=before;render();$('preset-presentation').value=value;throw error;}});
window.addEventListener('beforeunload',event=>{if(changed){event.preventDefault();event.returnValue='';}});
window.addEventListener('pagehide',()=>{api.token='';clearTimeout(timer);});
