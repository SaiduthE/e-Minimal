/* e-Minimal web UI shell — see docs/web-ui.md */
(function () {
  'use strict';
  var d = document, body = d.body;
  var page = body.getAttribute('data-page') || '';
  // A sub-page (Pictures) lights its parent's drawer link.
  var nav = body.getAttribute('data-nav') || page;
  var title = body.getAttribute('data-title') || 'e-Minimal';
  var $ = function (id) { return d.getElementById(id); };
  var esc = function (s) {
    return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  };
  var FOCUSABLE = 'a[href],button:not([disabled]),input:not([disabled]):not([type=hidden]),select:not([disabled]),textarea:not([disabled]),[tabindex]:not([tabindex="-1"])';

  /* Shell */
  body.classList.add('has-shell');
  body.insertAdjacentHTML('afterbegin',
    '<header class="topbar">' +
      '<button class="menu-btn" id="menuBtn" aria-controls="drawer" aria-expanded="false" aria-label="Menu">' +
        '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><path d="M4 7h16M4 12h16M4 17h16"/></svg>' +
      '</button>' +
      '<div class="topbar-title" id="topbarTitle"></div>' +
      '<div class="topbar-status" id="topbarStatus"></div>' +
    '</header>' +
    '<div class="backdrop" id="backdrop"></div>' +
    '<nav class="drawer" id="drawer" aria-label="Pages">' +
      '<a class="brand" href="/">e-Minimal</a>' +
      '<a href="/" data-page="home">Home</a>' +
      '<a href="/files" data-page="files">Files</a>' +
      '<a href="/convert" data-page="convert">Convert</a>' +
      '<a href="/settings" data-page="settings">Settings</a>' +
      '<a href="/dashboards" data-page="dashboards">Dashboards</a>' +
      '<a href="/fonts" data-page="fonts">Fonts</a>' +
      '<a href="/dictionaries" data-page="dictionaries">Dictionaries</a>' +
      '<div class="drawer-foot" id="drawerFoot"></div>' +
    '</nav>');

  var menuBtn = $('menuBtn'), drawer = $('drawer'), backdrop = $('backdrop');
  $('topbarTitle').textContent = title;
  var active = drawer.querySelector('a[data-page="' + nav + '"]');
  if (active) { active.classList.add('active'); active.setAttribute('aria-current', nav === page ? 'page' : 'true'); }

  function isOpen() { return body.classList.contains('drawer-open'); }
  function setDrawer(open) {
    body.classList.toggle('drawer-open', open);
    menuBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (open) (active || drawer.querySelector('a')).focus();
    else menuBtn.focus();
  }
  menuBtn.addEventListener('click', function () { setDrawer(!isOpen()); });
  backdrop.addEventListener('click', function () { setDrawer(false); });
  drawer.addEventListener('click', function (e) { if (e.target.closest('a')) body.classList.remove('drawer-open'); });
  var wide = window.matchMedia('(min-width: 900px)');
  var onWide = function () { if (wide.matches && isOpen()) body.classList.remove('drawer-open'); };
  if (wide.addEventListener) wide.addEventListener('change', onWide); else wide.addListener(onWide);

  /* Status: topbar chip and drawer foot */
  fetch('/api/status', { cache: 'no-store' })
    .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
    .then(function (s) {
      var mode = s.mode === 'AP' ? 'Hotspot' : 'Joined';
      $('topbarStatus').innerHTML = '<span class="chip">' + esc(mode + ' · ' + (s.ip || '')) + '</span>';
      var lines = [];
      if (s.device) lines.push(esc(s.device));
      if (s.ip) lines.push(esc(s.ip));
      if (s.freeHeap) lines.push(Math.round(s.freeHeap / 1024) + ' KB free');
      if (s.version) lines.push('Version ' + esc(s.version));
      $('drawerFoot').innerHTML = lines.map(function (l) { return '<div>' + l + '</div>'; }).join('');
    })
    .catch(function () {});

  /* Modals: open/close with focus trap, Esc, backdrop tap, focus return */
  var stack = [];
  function openModal(el, opts) {
    opts = opts || {};
    var rec = { el: el, opener: d.activeElement, dismiss: opts.dismiss !== false, onClose: opts.onClose };
    stack.push(rec);
    el.classList.add('open');
    var box = el.querySelector('.modal') || el;
    box.setAttribute('role', 'dialog');
    box.setAttribute('aria-modal', 'true');
    // Scope every alternative of the selector list, not just the first one.
    var inBody = FOCUSABLE.split(',').map(function (sel) { return '.modal-body ' + sel; }).join(',');
    var first = el.querySelector('[autofocus]') || el.querySelector(inBody) || el.querySelector(FOCUSABLE);
    if (first) setTimeout(function () { first.focus(); }, 0);
  }
  function closeModal(el, result) {
    var i = stack.length - 1;
    while (i >= 0 && el && stack[i].el !== el) i--;
    if (i < 0) return;
    var rec = stack.splice(i, 1)[0];
    rec.el.classList.remove('open');
    if (rec.opener && rec.opener.focus) rec.opener.focus();
    if (rec.onClose) rec.onClose(result);
  }
  d.addEventListener('click', function (e) {
    var top = stack[stack.length - 1];
    if (top && top.dismiss && e.target === top.el) closeModal(top.el, false);
  });
  d.addEventListener('keydown', function (e) {
    var top = stack[stack.length - 1];
    if (e.key === 'Escape') {
      if (top) { if (top.dismiss) { e.preventDefault(); closeModal(top.el, false); } }
      else if (isOpen()) { e.preventDefault(); setDrawer(false); }
    } else if (e.key === 'Tab' && top) {
      var f = top.el.querySelectorAll(FOCUSABLE);
      if (!f.length) { e.preventDefault(); return; }
      var a = f[0], z = f[f.length - 1];
      if (e.shiftKey && (d.activeElement === a || !top.el.contains(d.activeElement))) { e.preventDefault(); z.focus(); }
      else if (!e.shiftKey && (d.activeElement === z || !top.el.contains(d.activeElement))) { e.preventDefault(); a.focus(); }
    }
  });

  /* Toast */
  var toastEl, toastTimer;
  function toast(text, kind) {
    if (!toastEl) {
      toastEl = d.createElement('div');
      toastEl.setAttribute('role', 'status');
      toastEl.setAttribute('aria-live', 'polite');
      body.appendChild(toastEl);
    }
    clearTimeout(toastTimer);
    toastEl.className = 'toast' + (kind ? ' toast-' + kind : '');
    toastEl.textContent = text;
    var bar = d.querySelector('.savebar.show');
    toastEl.style.bottom = bar ? (bar.offsetHeight + 16) + 'px' : '';
    void toastEl.offsetWidth;
    toastEl.classList.add('show');
    toastTimer = setTimeout(function () { toastEl.classList.remove('show'); }, 3000);
  }

  /* Confirm */
  var confirmEl;
  function confirm(text, opts) {
    opts = opts || {};
    if (!confirmEl) {
      confirmEl = d.createElement('div');
      confirmEl.className = 'modal-backdrop';
      confirmEl.innerHTML =
        '<div class="modal" aria-labelledby="uiConfirmText">' +
          '<div class="modal-body"><p id="uiConfirmText"></p></div>' +
          '<div class="modal-foot"><button type="button" class="btn" data-x="cancel">Cancel</button>' +
          '<button type="button" class="btn" data-x="ok"></button></div></div>';
      body.appendChild(confirmEl);
    }
    var ok = confirmEl.querySelector('[data-x=ok]'), cancel = confirmEl.querySelector('[data-x=cancel]');
    confirmEl.querySelector('p').textContent = text;
    ok.textContent = opts.ok || 'OK';
    ok.className = 'btn ' + (opts.danger ? 'btn-danger' : 'btn-primary');
    return new Promise(function (resolve) {
      ok.onclick = function () { closeModal(confirmEl, true); };
      cancel.onclick = function () { closeModal(confirmEl, false); };
      openModal(confirmEl, { onClose: function (r) { resolve(!!r); } });
      setTimeout(function () { (opts.danger ? cancel : ok).focus(); }, 0);
    });
  }

  /* Folders: makes a path one segment at a time (POST /mkdir answers 400 "already exists" for a segment that is there) */
  var BAD_NAME = /[\/\\:*?"<>|\u0000-\u001f]/;
  function joinPath(folder, name) {
    return (folder === '/' ? '' : folder) + '/' + name;
  }
  function mkdir(parent, name) {
    var fd = new FormData();
    fd.append('name', name);
    fd.append('path', parent);
    return fetch('/mkdir', { method: 'POST', body: fd }).then(function (r) {
      if (r.ok) return;
      return r.text().then(function (t) {
        if (!/already exists/i.test(t)) throw new Error(t || ('Could not create ' + joinPath(parent, name)));
      });
    });
  }
  function ensureFolder(path) {
    var segs = String(path || '').split('/').filter(Boolean);
    var parent = '/';
    var p = Promise.resolve();
    segs.forEach(function (s) {
      var at = parent;
      p = p.then(function () { return mkdir(at, s); });
      parent = joinPath(parent, s);
    });
    return p.then(function () { return '/' + segs.join('/'); });
  }

  /* New-folder dialog: resolves to the created path, or '' when cancelled */
  var folderEl;
  function askNewFolder(parent) {
    if (!folderEl) {
      folderEl = d.createElement('div');
      folderEl.className = 'modal-backdrop';
      folderEl.innerHTML =
        '<form class="modal" aria-labelledby="uiFolderTitle">' +
          '<div class="modal-head"><h2 id="uiFolderTitle">New folder</h2></div>' +
          '<div class="modal-body">' +
            '<p class="field-hint folder-parent"></p>' +
            '<div class="field"><label for="uiFolderName">Name</label>' +
            '<input type="text" id="uiFolderName" maxlength="100" autocomplete="off" autofocus></div>' +
            '<p class="field-hint folder-err" role="alert"></p>' +
          '</div>' +
          '<div class="modal-foot"><button type="button" class="btn" data-x="cancel">Cancel</button>' +
          '<button type="submit" class="btn btn-primary">Create</button></div></form>';
      body.appendChild(folderEl);
    }
    var form = folderEl.querySelector('form'), input = folderEl.querySelector('input');
    var err = folderEl.querySelector('.folder-err'), go = form.querySelector('[type=submit]');
    folderEl.querySelector('.folder-parent').textContent = 'In ' + parent;
    input.value = '';
    err.textContent = '';
    return new Promise(function (resolve) {
      form.querySelector('[data-x=cancel]').onclick = function () { closeModal(folderEl, ''); };
      form.onsubmit = function (e) {
        e.preventDefault();
        var name = input.value.trim();
        if (!name || BAD_NAME.test(name) || name.charAt(0) === '.') {
          err.textContent = 'Use a name without / \\ : * ? " < > | and not starting with a dot.';
          return;
        }
        go.disabled = true;
        mkdir(parent, name).then(function () { closeModal(folderEl, joinPath(parent, name)); },
          function (ex) { err.textContent = ex.message; })
          .then(function () { go.disabled = false; });
      };
      openModal(folderEl, { onClose: function (r) { resolve(r || ''); } });
    });
  }

  /* Folder picker. The host element (a <button>) shows the chosen path; clicking it opens one shared dialog that
     browses the card's folders (built client-side from GET /api/folders). opts: {value, onChange(path)}.
     el.value reads and writes the path, and a bubbling 'change' fires when the user picks another folder, so
     pages treat the host like the text field it replaces. A value= attribute set before this runs wins over opts.value. */
  var TRUNC_NOTE = 'Showing the first 256 folders';
  var tree = null, treeLoad = null, treeSeq = 0, painters = [];
  function normPath(p) { return '/' + String(p || '').split('/').filter(Boolean).join('/'); }
  function parentOf(p) { var i = p.lastIndexOf('/'); return i <= 0 ? '/' : p.slice(0, i); }
  function leafOf(p) { return p.slice(p.lastIndexOf('/') + 1); }
  function byLeaf(a, b) { return leafOf(a).localeCompare(leafOf(b), undefined, { numeric: true, sensitivity: 'base' }); }
  function byFull(a, b) { return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' }); }

  // Folder tree from the flat list: kids[path] = sorted child paths. Keys all start with "/".
  function buildTree(list, truncated, failed) {
    var t = { kids: { '/': [] }, truncated: truncated, failed: failed };
    t.add = function (raw) {
      var p = normPath(raw);
      if (t.kids[p]) return;
      t.kids[p] = [];
      var par = parentOf(p);
      t.add(par);
      t.kids[par].push(p);
      t.kids[par].sort(byLeaf);
    };
    list.forEach(t.add);
    return t;
  }
  function paintAll() { painters.forEach(function (f) { f(); }); }
  function loadTree(force) {
    if (treeLoad && !force) return treeLoad;
    var seq = ++treeSeq;
    var p = fetch('/api/folders', { cache: 'no-store' })
      .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
      .then(function (j) { if (seq === treeSeq) tree = buildTree(j.folders || [], !!j.truncated, false); },
        function () { if (seq === treeSeq && !tree) tree = buildTree([], false, true); })
      .then(function () { paintAll(); return tree; });
    treeLoad = p;
    return p;
  }
  function nearestFolder(p) {
    p = normPath(p);
    while (tree && p !== '/' && !tree.kids[p]) p = parentOf(p);
    return p;
  }

  var fpEl;
  function mk(tag, cls, text) {
    var n = d.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }
  function fpBuild() {
    if (fpEl) return;
    fpEl = d.createElement('div');
    fpEl.className = 'modal-backdrop';
    fpEl.innerHTML =
      '<div class="modal fp-modal" aria-labelledby="fpTitle">' +
        '<div class="modal-head"><h2 id="fpTitle">Choose folder</h2>' +
          '<button type="button" class="btn" data-x="new">New folder</button></div>' +
        '<div class="fp-bar"><button type="button" class="btn" data-x="up" aria-label="Up one folder">↑ Up</button>' +
          '<nav class="crumbs fp-crumbs" aria-label="Folder"></nav></div>' +
        '<div class="fp-search"><input type="search" placeholder="Search all folders" aria-label="Search all folders" ' +
          'autocomplete="off" autocapitalize="off" spellcheck="false"></div>' +
        '<div class="modal-body fp-body"></div>' +
        '<p class="field-hint fp-note" hidden></p>' +
        '<div class="modal-foot"><button type="button" class="btn" data-x="cancel">Cancel</button>' +
          '<button type="button" class="btn btn-primary" data-x="select">Select this folder</button></div></div>';
    body.appendChild(fpEl);
    var q = function (s) { return fpEl.querySelector(s); };
    var input = q('input');
    function go(p) {
      var st = fpEl.st;
      if (!st) return;
      st.browse = p;
      st.query = '';
      input.value = '';
      fpDraw();
      q('.fp-body').scrollTop = 0;
    }
    q('[data-x=cancel]').onclick = function () { closeModal(fpEl, ''); };
    q('[data-x=select]').onclick = function () { if (fpEl.st) closeModal(fpEl, fpEl.st.browse); };
    q('[data-x=up]').onclick = function () { if (fpEl.st) go(parentOf(fpEl.st.browse)); };
    q('[data-x=new]').onclick = function () {
      var st = fpEl.st;
      if (!st) return;
      askNewFolder(st.browse).then(function (made) {
        if (!made) return;
        if (tree) tree.add(made);
        go(made);
        loadTree(true).then(function () { if (fpEl.st) fpDraw(); });
      });
    };
    function onPick(e) {
      var b = e.target.closest('button[data-path]');
      if (b) go(b.getAttribute('data-path'));
    }
    q('.fp-crumbs').addEventListener('click', onPick);
    q('.fp-body').addEventListener('click', onPick);
    input.addEventListener('input', function () {
      if (!fpEl.st) return;
      fpEl.st.query = input.value.trim().toLowerCase();
      fpDraw();
      q('.fp-body').scrollTop = 0;
    });
    input.addEventListener('keydown', function (e) {
      if (e.key !== 'Enter') return;
      e.preventDefault();
      var first = q('.fp-body button[data-path]');
      if (first && fpEl.st && fpEl.st.query) go(first.getAttribute('data-path'));
    });
  }
  function fpRow(path, withPath, selected) {
    var b = mk('button', 'fp-row');
    b.type = 'button';
    b.setAttribute('data-path', path);
    if (selected) b.setAttribute('aria-current', 'true');
    b.appendChild(mk('span', 'fp-ico', '📁'));
    var main = mk('span', 'fp-row-main', leafOf(path));
    if (withPath) main.appendChild(mk('span', 'fp-row-meta', path));
    b.appendChild(main);
    var n = tree && tree.kids[path] ? tree.kids[path].length : 0;
    if (!withPath && n) {
      b.appendChild(mk('span', 'fp-count', String(n)));
      b.appendChild(mk('span', 'fp-chev', '›'));
    }
    return b;
  }
  function fpDraw() {
    var st = fpEl.st;
    if (!st) return;
    var crumbs = fpEl.querySelector('.fp-crumbs'), list = fpEl.querySelector('.fp-body'), note = fpEl.querySelector('.fp-note');
    var segs = st.browse.split('/').filter(Boolean), at = '', i;
    crumbs.textContent = '';
    for (i = -1; i < segs.length; i++) {
      if (i >= 0) at += '/' + segs[i];
      var label = i < 0 ? 'SD card' : segs[i];
      if (i === segs.length - 1) {
        var cur = mk('span', 'fp-crumb-cur', label);
        cur.setAttribute('aria-current', 'page');
        crumbs.appendChild(cur);
      } else {
        var cb = mk('button', 'fp-crumb', label);
        cb.type = 'button';
        cb.setAttribute('data-path', i < 0 ? '/' : at);
        crumbs.appendChild(cb);
      }
    }
    fpEl.querySelector('[data-x=up]').disabled = st.browse === '/';
    list.textContent = '';
    if (!tree) {
      list.appendChild(mk('p', 'fp-empty', 'Loading folders…'));
    } else if (st.query) {
      var hits = Object.keys(tree.kids).filter(function (p) {
        return p !== '/' && p.toLowerCase().indexOf(st.query) >= 0;
      }).sort(byFull);
      hits.forEach(function (p) { list.appendChild(fpRow(p, true, false)); });
      if (!hits.length) list.appendChild(mk('p', 'fp-empty', 'No folder matches “' + st.query + '”.'));
    } else {
      var kids = tree.kids[st.browse] || [];
      kids.forEach(function (p) { list.appendChild(fpRow(p, false, p === st.current)); });
      if (!kids.length) list.appendChild(mk('p', 'fp-empty', 'No subfolders here.'));
    }
    var msg = tree && tree.failed ? 'Could not load the folder list.' : (tree && tree.truncated ? TRUNC_NOTE : '');
    note.textContent = msg;
    note.hidden = !msg;
  }
  // Resolves to the chosen folder, or '' when cancelled.
  function chooseFolder(start) {
    fpBuild();
    var st = fpEl.st = { browse: nearestFolder(start), query: '', current: normPath(start) };
    fpEl.querySelector('input').value = '';
    return new Promise(function (resolve) {
      openModal(fpEl, { onClose: function (r) { fpEl.st = null; resolve(r || ''); } });
      fpDraw();
      if (window.matchMedia && window.matchMedia('(pointer: fine)').matches) {
        setTimeout(function () { fpEl.querySelector('input').focus(); }, 0);
      }
      loadTree(true).then(function () {
        if (fpEl.st !== st) return;
        st.browse = nearestFolder(st.browse);
        fpDraw();
      });
    });
  }

  function folderPicker(el, opts) {
    opts = opts || {};
    var attr = el.getAttribute('value');
    var current = normPath(attr != null ? attr : opts.value);
    function paint() {
      el.textContent = '';
      el.appendChild(mk('span', 'fp-ico', '📁'));
      el.appendChild(mk('span', 'fp-path', current));
      if (tree && !tree.failed && !tree.kids[current]) el.appendChild(mk('span', 'fp-new', '(new)'));
      el.appendChild(mk('span', 'fp-change', 'Change'));
      el.title = current;
    }
    function setPath(p, fire) {
      var next = normPath(p), changed = next !== current;
      current = next;
      paint();
      if (fire && changed) {
        el.dispatchEvent(new Event('change', { bubbles: true }));
        if (opts.onChange) opts.onChange(current);
      }
    }
    Object.defineProperty(el, 'value', {
      configurable: true,
      get: function () { return current; },
      set: function (v) { setPath(v, false); }
    });
    el.classList.add('fp-field');
    el.setAttribute('aria-haspopup', 'dialog');
    el.addEventListener('click', function () {
      chooseFolder(current).then(function (p) { if (p) setPath(p, true); });
    });
    painters.push(paint);
    paint();
    return {
      ready: loadTree(),
      get value() { return current; },
      set: function (p) { setPath(p, false); },
      reload: function () { return loadTree(true); },
      ensure: function () { return ensureFolder(current); }
    };
  }

  /* Book names: a readable title and a series number from a file name */
  var TRAIL_PREFIXED = /^(.+?)[\s,:(\[\-–]*(?:book|vol(?:ume)?\.?|part|bk\.?)\s*(\d{1,3}(?:\.\d{1,2})?)[)\]]?$/i;
  var TRAIL_HASH = /^(.+?)[\s,:(\[\-–]*#\s*(\d{1,3}(?:\.\d{1,2})?)[)\]]?$/;
  var TRAIL_BARE = /^(.+?)\s+(\d{1,2}(?:\.\d)?)$/;
  var LEAD_PREFIXED = /^(?:book|vol(?:ume)?\.?|part|bk\.?|#)\s*(\d{1,3}(?:\.\d{1,2})?)\s*[-–:]*\s*(.+)$/i;
  var LEAD_BARE = /^(\d{1,3}(?:\.\d)?)\s*[-–:.)]\s*(.+)$/;
  function cleanBookName(fileName) {
    var stem = String(fileName).replace(/\.[A-Za-z0-9]{1,5}$/, '');
    var m = LEAD_BARE.exec(stem);
    var s = stem.replace(/[_.]+/g, ' ').replace(/\s+/g, ' ').trim();
    var num = '', title = s;
    if (m) { num = m[1]; title = m[2].replace(/[_.]+/g, ' ').replace(/\s+/g, ' ').trim(); }
    else if ((m = LEAD_PREFIXED.exec(s))) { num = m[1]; title = m[2].trim(); }
    else if ((m = TRAIL_PREFIXED.exec(s)) || (m = TRAIL_HASH.exec(s)) || (m = TRAIL_BARE.exec(s))) {
      num = m[2]; title = m[1].replace(/[\s,:(\[\-–]+$/, '').trim();
    }
    if (!title || /^(book|vol|volume|part|bk)$/i.test(title)) { title = s; num = ''; }
    if (num) num = String(parseFloat(num));
    return { title: title, num: num };
  }

  /* Open Library search (called from the browser, so the phone needs internet).
     Resolves to [{title, author, year, genre}]. Rejects with a readable message. */
  function searchOpenLibrary(query) {
    var ctl = new AbortController();
    var timer = setTimeout(function () { ctl.abort(); }, 8000);
    var url = 'https://openlibrary.org/search.json?q=' + encodeURIComponent(query) +
      '&fields=title,author_name,first_publish_year,subject,key&limit=8';
    return fetch(url, { signal: ctl.signal }).then(function (r) {
      if (!r.ok) throw new Error('Open Library answered ' + r.status + '.');
      return r.json();
    }).then(function (j) {
      return (j.docs || []).map(function (x) {
        return {
          title: x.title || '',
          author: (x.author_name && x.author_name[0]) || '',
          year: x.first_publish_year || '',
          genre: (x.subject && x.subject[0]) || ''
        };
      });
    }, function (e) {
      if (e && e.message && /^Open Library/.test(e.message)) throw e;
      throw new Error('No internet on this connection. Fetching details needs a phone or computer that is online (not only on the reader’s hotspot).');
    }).then(function (r) { clearTimeout(timer); return r; }, function (e) { clearTimeout(timer); throw e; });
  }

  window.ui = {
    toast: toast, confirm: confirm, openModal: openModal, closeModal: closeModal,
    folderPicker: folderPicker, ensureFolder: ensureFolder, joinPath: joinPath,
    cleanBookName: cleanBookName, searchOpenLibrary: searchOpenLibrary
  };
})();
