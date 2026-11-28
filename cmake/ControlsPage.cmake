# Adapt the pinned shared Controls page without modifying the submodule checkout.
set(shared_page "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pxs-qt/src/controls/pxs/ControlsPage.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${shared_page}")
file(READ "${shared_page}" controls_page)
string(REPLACE "\r\n" "\n" controls_page "${controls_page}")

function(replace_controls_page original replacement)
  string(FIND "${controls_page}" "${original}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "The shared Controls page changed; update NyxMap's Controls page adaptations")
  endif()
  string(REPLACE "${original}" "${replacement}" controls_page "${controls_page}")
  set(controls_page "${controls_page}" PARENT_SCOPE)
endfunction()

replace_controls_page(
  [=[    mode.onchange = () => { p.mode = Number(mode.value); build(); save(Object.assign({}, p)); }; build();
    if (widget) {]=]
  [=[    const widgets = el('div');
    mode.onchange = () => { p.mode = Number(mode.value); build(); widgets.hidden = p.mode !== 2; save(Object.assign({}, p)); };
    build();
    if (widget) {
      host.appendChild(widgets);
      widgets.hidden = p.mode !== 2;]=])
replace_controls_page(
  [=[host.appendChild(el('label', '', 'Widgets for these settings'));]=]
  [=[widgets.appendChild(el('label', '', 'Widgets for these settings'));]=])
replace_controls_page(
  [=[['wall', 'Wall'], ['rate', 'Speed']].forEach(k => { const b = el('button', '', '+ ' + k[1]); b.title = 'Add a slider for the ' + k[1].toLowerCase(); b.onclick = () => widget(k[0]); row.appendChild(b); }); host.appendChild(row);]=]
  [=[['wall', 'Wall']].forEach(k => { const b = el('button', '', '+ ' + k[1]); b.title = 'Add a slider for the ' + k[1].toLowerCase(); b.onclick = () => widget(k[0]); row.appendChild(b); }); widgets.appendChild(row);]=])

# Plain NyxMap multi-point pads already support a variable count. Give them
# the same add/remove affordance while retaining their shared range settings.
replace_controls_page(
  [=[field('Points', numeric(c.count, v => push({ count: Math.round(v) }), 1));]=]
  [=[const countRow = el('div', 'row'); let count = c.count;
      const setCount = v => { count = Math.max(1, Math.min(16, Math.round(v))); countInput.value = count; push({ count }); };
      const countInput = numeric(count, setCount, 1);
      const minus = el('button', '', '−'), plus = el('button', '', '+');
      minus.title = 'Remove a ball'; minus.onclick = () => { if (count > 1) setCount(count - 1); };
      plus.title = 'Add a ball'; plus.onclick = () => { if (count < 16) setCount(count + 1); };
      countRow.append(minus, countInput, plus); field('Balls', countRow);]=])

set(controls_page_include "${CMAKE_CURRENT_BINARY_DIR}/generated/controls-page")
file(CONFIGURE OUTPUT "${controls_page_include}/pxs/ControlsPage.h" CONTENT "${controls_page}" @ONLY)
target_include_directories(pxs_controls BEFORE PUBLIC "${controls_page_include}")

# Server.cpp includes the page relative to its own directory. Compile an
# identical copy next to the adapted header for both the page and its CSP hash.
set(shared_server "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pxs-qt/src/controls/Server.cpp")
configure_file("${shared_server}" "${controls_page_include}/Server.cpp" COPYONLY)
set_source_files_properties("${shared_server}" TARGET_DIRECTORY pxs_controls PROPERTIES HEADER_FILE_ONLY ON)
target_sources(pxs_controls PRIVATE "${controls_page_include}/Server.cpp")
