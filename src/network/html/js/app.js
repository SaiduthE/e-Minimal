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
  var NEW_FOLDER = '\u0001new';
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

  function sortKey(p) { return p.toLowerCase().split('/'); }
  function byPath(a, b) {
    var x = sortKey(a), y = sortKey(b);
    for (var i = 0; i < x.length && i < y.length; i++) {
      if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return x.length - y.length;
  }

  /* Fills a <select> with the card's folders plus "New folder…". opts: {value, onChange(path)}.
     sel.value reads and writes the chosen path, so pages can treat it like the text field it replaces. */
  function folderPicker(sel, opts) {
    opts = opts || {};
    var list = ['/'], current = '/', busy = false;
    var nativeValue = Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value');
    function norm(p) { var s = String(p || '').split('/').filter(Boolean); return '/' + s.join('/'); }
    function render() {
      var all = list.slice();
      if (all.indexOf(current) < 0) all.push(current);
      all.sort(byPath);
      sel.innerHTML = '';
      all.forEach(function (p) {
        var o = d.createElement('option');
        var depth = p === '/' ? 0 : p.split('/').length - 1;
        var leaf = p === '/' ? '/ (SD card)' : p.slice(p.lastIndexOf('/') + 1);
        o.value = p;
        o.textContent = new Array(depth + 1).join('  ') + leaf + (list.indexOf(p) < 0 ? ' (new)' : '');
        sel.appendChild(o);
      });
      var n = d.createElement('option');
      n.value = NEW_FOLDER;
      n.textContent = 'New folder…';
      sel.appendChild(n);
      nativeValue.set.call(sel, current);
    }
    function load() {
      return fetch('/api/folders', { cache: 'no-store' })
        .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
        .then(function (j) { list = (j.folders && j.folders.length) ? j.folders : ['/']; })
        .catch(function () { list = ['/']; });
    }
    function setPath(p, fire) {
      current = norm(p);
      render();
      // The change listener below reports it to opts.onChange.
      if (fire) sel.dispatchEvent(new Event('change', { bubbles: true }));
    }
    Object.defineProperty(sel, 'value', {
      configurable: true,
      get: function () { return nativeValue.get.call(sel) === NEW_FOLDER ? current : nativeValue.get.call(sel); },
      set: function (v) { setPath(v, false); }
    });
    sel.addEventListener('change', function (e) {
      if (busy) { e.stopImmediatePropagation(); return; }
      if (nativeValue.get.call(sel) !== NEW_FOLDER) {
        current = nativeValue.get.call(sel);
        if (opts.onChange) opts.onChange(current);
        return;
      }
      // The dialog is not a real change: hide this event until a folder exists.
      e.stopImmediatePropagation();
      busy = true;
      askNewFolder(current).then(function (made) {
        busy = false;
        if (!made) { render(); return; }
        return load().then(function () { setPath(made, true); });
      });
    }, true);
    current = norm(opts.value);
    render();
    var ready = load().then(function () { render(); });
    return {
      ready: ready,
      get value() { return current; },
      set: function (p) { setPath(p, false); },
      reload: function () { return load().then(render); },
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
