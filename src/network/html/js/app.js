/* e-Minimal web UI shell — see docs/web-ui.md */

/* Session. The reader hands out a cookie (ems) after the access code; a 401 anywhere means the session is gone, and
   reloading lands on the login page (every page route answers it while signed out). The WebSocket cannot carry
   the cookie into its START line by itself, so pages read the token with emsToken(). */
(function () {
  'use strict';
  window.emsToken = function () {
    var m = /(?:^|;\s*)ems=([0-9a-fA-F]+)/.exec(document.cookie);
    return m ? m[1] : '';
  };
  var reloading = false;
  function signedOut() {
    if (reloading) return;
    reloading = true;
    location.reload();
  }
  var nativeFetch = window.fetch;
  window.fetch = function () {
    return nativeFetch.apply(this, arguments).then(function (r) {
      if (r.status === 401) signedOut();
      return r;
    });
  };
  var nativeSend = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.send = function () {
    this.addEventListener('load', function () { if (this.status === 401) signedOut(); });
    return nativeSend.apply(this, arguments);
  };
})();

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

  /* Online lookups run in the browser, so the phone needs internet. A candidate is
     {title, author, year, genre, series, seriesIndex, src}. Rejects with a readable message. */
  var NO_NET = 'No internet on this connection. Fetching details needs a phone or computer that is online (not only on the reader’s hotspot).';
  function fetchJson(url, name) {
    var ctl = new AbortController();
    var timer = setTimeout(function () { ctl.abort(); }, 8000);
    return fetch(url, { signal: ctl.signal }).then(function (r) {
      if (!r.ok) throw new Error(name + ' answered ' + r.status + '.');
      return r.json();
    }, function () {
      throw new Error(NO_NET);
    }).then(function (r) { clearTimeout(timer); return r; }, function (e) { clearTimeout(timer); throw e; });
  }

  function searchOpenLibrary(query) {
    var url = 'https://openlibrary.org/search.json?q=' + encodeURIComponent(query) +
      '&fields=title,author_name,first_publish_year,subject,key&limit=8';
    return fetchJson(url, 'Open Library').then(function (j) {
      return (j.docs || []).map(function (x) {
        return {
          title: x.title || '',
          author: (x.author_name && x.author_name[0]) || '',
          year: x.first_publish_year || '',
          genre: (x.subject && x.subject[0]) || '',
          src: 'Open Library'
        };
      });
    });
  }

  function searchGoogleBooks(query) {
    var url = 'https://www.googleapis.com/books/v1/volumes?q=' + encodeURIComponent(query) +
      '&maxResults=5&printType=books';
    return fetchJson(url, 'Google Books').then(function (j) {
      return (j.items || []).map(function (it) {
        var v = it.volumeInfo || {}, si = it.seriesInfo || v.seriesInfo || {};
        return {
          title: v.title || '',
          author: (v.authors && v.authors[0]) || '',
          year: String(v.publishedDate || '').slice(0, 4),
          genre: (v.categories && v.categories[0]) || '',
          series: si.shortSeriesBookTitle || '',
          seriesIndex: si.bookDisplayNumber ? String(si.bookDisplayNumber) : '',
          src: 'Google Books'
        };
      });
    });
  }

  function titleTokens(s) {
    return String(s || '').toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '')
      .replace(/[^a-z0-9]+/g, ' ').trim().split(' ')
      .filter(function (t) { return t && t !== 'the' && t !== 'a' && t !== 'an'; });
  }
  function authorKeys(s) {
    return titleTokens(s).filter(function (t) { return t.length > 2; });
  }
  /* 0..1: shared title words over the longer title, nudged by whether the author agrees. */
  function scoreCandidate(c, title, author) {
    var a = titleTokens(title), b = titleTokens(c.title);
    if (!a.length || !b.length) return 0;
    var inter = 0;
    a.forEach(function (t) { if (b.indexOf(t) >= 0) inter++; });
    var score = inter / Math.max(a.length, b.length);
    var want = authorKeys(author);
    if (want.length) {
      var have = authorKeys(c.author);
      score += want.some(function (t) { return have.indexOf(t) >= 0; }) ? 0.1 : -0.2;
    }
    return Math.max(0, Math.min(1, score));
  }

  /* Both sources, merged, best first. Fails only when every source fails. */
  function fetchBookCandidates(title, author) {
    var q = (String(title || '') + ' ' + String(author || '')).trim();
    var jobs = [searchOpenLibrary(q), searchGoogleBooks(q)].map(function (p) {
      return p.then(function (v) { return { v: v }; }, function (e) { return { e: e }; });
    });
    return Promise.all(jobs).then(function (rs) {
      var ok = rs.filter(function (r) { return r.v; });
      if (!ok.length) throw rs[0].e;
      var byKey = {}, out = [];
      ok.forEach(function (r) {
        r.v.forEach(function (c) {
          if (!c.title) return;
          var key = titleTokens(c.title).join(' ') + '|' + (authorKeys(c.author).pop() || '');
          var seen = byKey[key];
          if (seen) {
            ['author', 'year', 'genre', 'series', 'seriesIndex'].forEach(function (k) { if (!seen[k] && c[k]) seen[k] = c[k]; });
            seen.both = true;
            seen.src += ' + ' + c.src;
            return;
          }
          c.score = scoreCandidate(c, title, author);
          byKey[key] = c;
          out.push(c);
        });
      });
      out.forEach(function (c, i) { c.rank = c.score + (c.both ? 0.02 : 0) - i * 0.0001; });
      out.sort(function (x, y) { return y.rank - x.rank; });
      return out;
    });
  }

  /* Candidate buttons in box; onPick(candidate) runs on a tap. */
  function renderBookCandidates(box, cands, onPick) {
    box.textContent = '';
    cands.forEach(function (c) {
      var pick = mk('button', null, c.title);
      pick.type = 'button';
      pick.appendChild(mk('small', null, [c.author, c.year, c.genre, c.src].filter(Boolean).join(' · ')));
      pick.addEventListener('click', function () { onPick(c); });
      box.appendChild(pick);
    });
  }

  /* Book details table for a batch of files (Files upload, Convert "Send to reader").
     Rows are plain objects: {fileName, title, author, series, seriesIndex, genre, status, titleChanged}. */
  var SERIES_NUM = /^([1-5]?\d{1,2}|600)(\.\d{1,2})?$/;  // the reader keeps 0-600
  var MATCHED = 0.85, WEAK = 0.4, FETCH_GAP = 400;
  var FIELDS = [['title', 'Title', 127], ['series', 'Series', 95], ['seriesIndex', '#', 11], ['author', 'Author', 95], ['genre', 'Genre', 63]];
  var STATUS_TEXT = { busy: 'searching…', matched: 'matched', check: 'check', none: 'not found', filled: 'filled', error: 'no result' };

  function bookDetailsTable(root, opts) {
    opts = opts || {};
    var rows = [], locked = false, running = false, stopFetch = false;
    var coll = { on: false, touched: false, name: '' };

    root.classList.add('bdt');
    root.textContent = '';
    root.hidden = true;
    var head = mk('div', 'bdt-top');
    var h = mk('h3', null, 'Book details ');
    var count = mk('span', 'small muted');
    h.appendChild(count);
    var allBtn = mk('button', 'btn btn-sm', 'Fetch details for all');
    allBtn.type = 'button';
    head.append(h, allBtn);
    var hint = mk('p', 'small muted', opts.hint || 'Optional. Titles come from the file names; EPUBs keep their own title unless you edit it.');
    var cw = mk('div', 'bdt-collection');
    var cl = mk('label'), cb = d.createElement('input');
    cb.type = 'checkbox';
    cl.append(cb, d.createTextNode(' Import as collection'));
    var cn = d.createElement('input');
    cn.type = 'text';
    cn.placeholder = 'Series name';
    cn.autocomplete = 'off';
    cn.maxLength = 95;
    cn.setAttribute('aria-label', 'Series name');
    cw.append(cl, cn);
    var list = mk('div', 'bdt-rows');
    var note = mk('p', 'field-hint bdt-note');
    note.setAttribute('role', 'status');
    root.append(head, hint, cw, list, note);

    function isEpub(r) { return /\.epub$/i.test(r.fileName); }
    function setInput(r, key, v) { r[key] = v; if (r.inputs) r.inputs[key].value = v; }

    function setStatus(r, kind) {
      r.status = kind;
      var chip = r.chip;
      if (!chip) return;
      chip.hidden = !kind;
      chip.textContent = STATUS_TEXT[kind] || '';
      chip.className = 'chip bdt-chip' + (kind === 'matched' || kind === 'filled' ? ' chip-ok' : kind === 'error' ? ' chip-danger' : '');
    }

    function expand(r, open) {
      r.open = open;
      if (r.node) {
        r.node.classList.toggle('open', open);
        r.moreBtn.textContent = open ? 'Less' : 'More';
        r.moreBtn.setAttribute('aria-expanded', String(open));
      }
    }

    function applyCollection() {
      if (!coll.on || !coll.name) return;
      rows.forEach(function (r) { setInput(r, 'series', coll.name); });
    }
    function numberRows() {
      rows.forEach(function (r) {
        setInput(r, 'seriesIndex', coll.on ? (r.parsed || String(r.order)) : r.parsed);
        if (!coll.on) setInput(r, 'series', '');
        else if (coll.name) setInput(r, 'series', coll.name);
      });
    }
    cb.addEventListener('change', function () { coll.on = cb.checked; numberRows(); });
    cn.addEventListener('input', function () { coll.touched = true; coll.name = cn.value; applyCollection(); });

    function fill(r, c, auto, weak) {
      if (c.title && !(auto && (r.touched.title || (weak && isEpub(r))))) { setInput(r, 'title', c.title); r.titleChanged = true; }
      if (c.author && !(auto && r.touched.author)) setInput(r, 'author', c.author);
      if (c.genre && !(auto && r.touched.genre)) setInput(r, 'genre', c.genre);
      // Series from these sources is unreliable: only a Google Books series fills an empty cell.
      if (c.series && !r.series.trim()) {
        setInput(r, 'series', c.series);
        if (c.seriesIndex && SERIES_NUM.test(c.seriesIndex)) setInput(r, 'seriesIndex', c.seriesIndex);
      }
    }

    function search(r) {
      var title = r.title.trim();
      if (!title) return Promise.reject(new Error('Enter a title first.'));
      setStatus(r, 'busy');
      return fetchBookCandidates(title, r.author.trim()).then(function (cands) {
        r.cands = cands.slice(0, 5);
        return r.cands;
      }, function (e) { setStatus(r, 'error'); throw e; });
    }
    function verdict(r) {
      var best = r.cands[0], s = best ? best.score : 0;
      return s >= MATCHED ? 'matched' : s >= WEAK ? 'check' : 'none';
    }

    function fetchOne(r) {
      if (r.fetching) return;
      if (r.candsEl.childNodes.length) { r.candsEl.textContent = ''; return; }
      r.fetching = true;
      r.fetchBtn.disabled = true;
      note.classList.remove('is-error');
      search(r).then(function (cands) {
        setStatus(r, verdict(r));
        if (!cands.length) {
          r.candsEl.appendChild(mk('p', 'small muted', 'No matches found online.'));
        } else {
          renderBookCandidates(r.candsEl, cands, function (c) {
            fill(r, c, false, false);
            r.candsEl.textContent = '';
            setStatus(r, 'filled');
            expand(r, true);
          });
        }
      }, function (e) {
        note.textContent = e.message;
        note.classList.add('is-error');
      }).then(function () { r.fetching = false; r.fetchBtn.disabled = locked; });
    }

    function sleep(ms) { return new Promise(function (res) { setTimeout(res, ms); }); }
    function fetchAll() {
      if (running) { stopFetch = true; return; }
      var todo = rows.filter(function (r) { return r.title.trim() && r.status !== 'matched' && r.status !== 'filled'; });
      if (!todo.length) return;
      running = true;
      stopFetch = false;
      allBtn.textContent = 'Stop';
      note.classList.remove('is-error');
      var tally = { matched: 0, check: 0, none: 0 }, failed = '';
      function finish() {
        running = false;
        allBtn.textContent = 'Fetch details for all';
        todo.forEach(function (r) { if (r.status === 'busy') setStatus(r, ''); });
        var parts = [tally.matched + ' matched', tally.check + ' to check', tally.none + ' not found'];
        note.textContent = failed ? failed : (stopFetch ? 'Stopped. ' : '') + parts.join(', ') + '. Check the details before sending.';
        note.classList.toggle('is-error', !!failed);
      }
      function next(i) {
        if (i >= todo.length || stopFetch || failed) return finish();
        var r = todo[i];
        note.textContent = 'Fetching ' + (i + 1) + ' of ' + todo.length + '…';
        r.candsEl.textContent = '';
        search(r).then(function () {
          var kind = verdict(r);
          if (kind !== 'none') { fill(r, r.cands[0], true, kind === 'check'); if (kind === 'check') expand(r, true); }
          setStatus(r, kind);
          tally[kind]++;
        }, function (e) { failed = e.message; }).then(function () {
          return i + 1 < todo.length && !failed && !stopFetch ? sleep(FETCH_GAP) : null;
        }).then(function () { next(i + 1); });
      }
      next(0);
    }
    allBtn.addEventListener('click', fetchAll);

    function input(r, key, label, max) {
      var el = d.createElement('input');
      el.type = 'text';
      el.placeholder = label;
      el.autocomplete = 'off';
      el.maxLength = max;
      el.value = r[key];
      if (key === 'seriesIndex') el.inputMode = 'decimal';
      el.setAttribute('aria-label', label + ' for ' + r.fileName);
      el.addEventListener('input', function () {
        r[key] = el.value;
        r.touched[key] = true;
        if (key === 'title') r.titleChanged = true;
        if (opts.onChange) opts.onChange();
      });
      r.inputs[key] = el;
      el.className = 'bdt-f-' + key;
      return el;
    }

    function build(r) {
      r.inputs = {};
      var node = r.node = mk('div', 'bdt-row');
      var line = mk('div', 'bdt-line');
      line.appendChild(mk('span', 'bdt-file', r.fileName));
      r.chip = mk('span', 'chip bdt-chip');
      line.appendChild(r.chip);
      r.fetchBtn = mk('button', 'btn btn-sm', 'Fetch');
      r.fetchBtn.type = 'button';
      r.fetchBtn.addEventListener('click', function () { fetchOne(r); });
      r.moreBtn = mk('button', 'btn btn-sm btn-ghost', 'More');
      r.moreBtn.type = 'button';
      r.moreBtn.addEventListener('click', function () { expand(r, !r.open); });
      line.append(r.fetchBtn, r.moreBtn);
      var f = mk('div', 'bdt-fields');
      FIELDS.forEach(function (k) { f.appendChild(input(r, k[0], k[1], k[2])); });
      r.candsEl = mk('div', 'meta-cands bdt-cands');
      node.append(line, f, r.candsEl);
      setStatus(r, r.status);
      expand(r, r.open);
      r.fetchBtn.disabled = r.moreBtn.disabled = locked;
      Object.keys(r.inputs).forEach(function (k) { r.inputs[k].disabled = locked; });
      return node;
    }

    function setFiles(files) {
      var old = rows.slice();
      rows = [];
      Array.prototype.forEach.call(files || [], function (f) {
        var name = f.name;
        var i = old.findIndex(function (o) { return o.fileName === name; });
        var r = i >= 0 ? old.splice(i, 1)[0] : null;
        if (!r) {
          var clean = cleanBookName(name);
          r = { fileName: name, parsed: clean.num, title: f.title != null ? f.title : clean.title, author: '', series: '',
                seriesIndex: clean.num, genre: '', status: '', titleChanged: false, touched: {}, open: false, cands: [] };
          if (coll.on) r.series = coll.name;
        }
        r.order = rows.length + 1;
        rows.push(r);
      });
      list.textContent = '';
      var frag = d.createDocumentFragment();
      rows.forEach(function (r) { frag.appendChild(build(r)); });
      list.appendChild(frag);
      root.hidden = !rows.length;
      count.textContent = rows.length === 1 ? '1 book' : rows.length + ' books';
      allBtn.hidden = !rows.length;
      if (!rows.length) { coll.touched = false; note.textContent = ''; }
    }

    function copy(r) {
      return { fileName: r.fileName, title: r.title, author: r.author, series: r.series, seriesIndex: r.seriesIndex,
               genre: r.genre, status: r.status, titleChanged: r.titleChanged };
    }

    return {
      setFiles: setFiles,
      count: function () { return rows.length; },
      getRow: function (i) { return rows[i] ? copy(rows[i]) : null; },
      rows: function () { return rows.map(copy); },
      lock: function (on) {
        locked = !!on;
        if (locked) stopFetch = true;
        root.querySelectorAll('input,button').forEach(function (el) { el.disabled = locked; });
      },
      // The collection's series name follows this (the destination folder) until the user types one.
      setDefaultName: function (name) {
        if (coll.touched) return;
        coll.name = cn.value = name || '';
        applyCollection();
      }
    };
  }

  /* POSTs the details worth storing for the book now at `path`; resolves true when saved or nothing to save.
     A book that carries its own title (EPUB) keeps it unless the title was edited or fetched; any other
     book's own title is its file name, so a cleaned title counts as a change. */
  function saveBookDetails(path, row, ownTitleKnown) {
    var out = {}, title = String(row.title || '').trim();
    var stem = path.slice(path.lastIndexOf('/') + 1).replace(/\.[^.]+$/, '');
    if (title && (ownTitleKnown ? row.titleChanged : title !== stem)) out.title = title;
    ['author', 'genre'].forEach(function (k) { var v = String(row[k] || '').trim(); if (v) out[k] = v; });
    var series = String(row.series || '').trim();
    if (series) {
      out.series = series;
      var num = String(row.seriesIndex || '').trim();
      if (SERIES_NUM.test(num)) out.seriesIndex = num;
    }
    if (!Object.keys(out).length) return Promise.resolve(true);
    out.path = path;
    return fetch('/api/book-meta', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(out)
    }).then(function (r) { return r.ok; }, function () { return false; });
  }

  window.ui = {
    toast: toast, confirm: confirm, openModal: openModal, closeModal: closeModal,
    folderPicker: folderPicker, ensureFolder: ensureFolder, joinPath: joinPath,
    cleanBookName: cleanBookName, searchOpenLibrary: searchOpenLibrary,
    fetchBookCandidates: fetchBookCandidates, renderBookCandidates: renderBookCandidates,
    bookDetailsTable: bookDetailsTable, saveBookDetails: saveBookDetails, seriesNumber: SERIES_NUM
  };
})();
