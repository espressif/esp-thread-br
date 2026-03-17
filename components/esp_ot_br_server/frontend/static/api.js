/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP Thread Border Router - Shared API utilities
 */
'use strict';

/**
 * Escape HTML special characters to prevent XSS when inserting
 * device-supplied strings into innerHTML.
 */
function escapeHtml(str) {
  if (typeof str !== 'string')
    str = String(str);
  return str.replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

function apiGet(url) {
  return fetch(url).then(function(r) { return r.json(); }).then(function(data) {
    try {
      sessionStorage.setItem('cache:' + url, JSON.stringify(data));
    } catch (e) {
    }
    return data;
  });
}

function apiPost(url, data) {
  return fetch(url, {
           method : 'POST',
           headers : {'Content-Type' : 'application/json'},
           body : JSON.stringify(data)
         })
      .then(function(r) { return r.json(); });
}

/**
 * Get cached response for a GET endpoint, or null if not available.
 * Use this to render immediately on page load before the live fetch completes.
 */
function getCached(url) {
  try {
    var raw = sessionStorage.getItem('cache:' + url);
    return raw ? JSON.parse(raw) : null;
  } catch (e) {
    return null;
  }
}

function showToast(msg, type) {
  var t = document.getElementById('_toast');
  if (!t) {
    t = document.createElement('div');
    t.id = '_toast';
    document.body.appendChild(t);
  }
  t.textContent = msg;
  t.className = 'toast toast-' + (type || 'info') + ' show';
  clearTimeout(t._tid);
  t._tid = setTimeout(function() { t.classList.remove('show'); }, 3500);
}

function showLoading(show) {
  var o = document.getElementById('_loading');
  if (!o) {
    o = document.createElement('div');
    o.id = '_loading';
    o.className = 'loading';
    o.innerHTML =
        '<div class="spinner" style="width:40px;height:40px"></div><div>Loading...</div>';
    document.body.appendChild(o);
  }
  if (show)
    o.classList.add('active');
  else
    o.classList.remove('active');
}

/**
 * Check Thread network state. Calls callback(role) where role is a string
 * like "disabled", "detached", "child", "router", "leader".
 * Returns true if the device is attached (child/router/leader).
 */
function checkThreadState(callback) {
  apiGet('/node/state')
      .then(function(role) {
        var r = (typeof role === 'string') ? role : '';
        var connected = (r === 'child' || r === 'router' || r === 'leader');
        if (callback)
          callback(r, connected);
      })
      .catch(function() {
        if (callback)
          callback('', false);
      });
}

/**
 * Disable all interactive elements inside a management page and show
 * a banner when the Thread network is not connected.
 * Call from management sub-pages (except network.html which is always active).
 */
function disableManagementPage() {
  /* Insert banner at top of .container */
  var container = document.querySelector('.container');
  if (container) {
    var banner = document.createElement('div');
    banner.className = 'disabled-banner';
    banner.innerHTML =
        'Thread network is not connected. <a href="/network.html">Go to Network</a> to join or form a network.';
    container.insertBefore(banner, container.firstChild);
  }
  /* Disable all buttons and inputs */
  var els = document.querySelectorAll('button, input, select, textarea');
  for (var i = 0; i < els.length; i++) {
    els[i].disabled = true;
  }
  /* Add visual overlay to cards */
  var cards = document.querySelectorAll('.card');
  for (var j = 0; j < cards.length; j++) {
    cards[j].classList.add('card-disabled');
  }
}

/* Build navigation bar and highlight current page. */
(function() {
var pages = [
  {href : '/index.html', label : 'Dashboard'}, {
    label : 'Management',
    children : [
      {href : '/network.html', label : 'Network'},
      {href : '/commission.html', label : 'Commissioner'},
      {href : '/addresses.html', label : 'Addresses'},
      {href : '/tools.html', label : 'Tools'}
    ]
  },
  {href : '/topology.html', label : 'Topology'},
  {href : '/about.html', label : 'About'}
];
var nav = document.getElementById('nav');
if (!nav)
  return;
var p = location.pathname;

var html =
    '<a href="/" class="nav-brand"><img src="/favicon.ico" width="22" height="22" alt="Espressif"> Espressif Thread Border Router</a><ul class="nav-links">';
for (var i = 0; i < pages.length; i++) {
  var pg = pages[i];
  if (pg.children) {
    /* Check if any child is active */
    var childActive = false;
    for (var c = 0; c < pg.children.length; c++) {
      if (p === pg.children[c].href) {
        childActive = true;
        break;
      }
    }
    html += '<li class="nav-dropdown">';
    html += '<a href="#" class="nav-dropdown-toggle' +
            (childActive ? ' active' : '') + '">' + pg.label + '</a>';
    html += '<ul class="nav-dropdown-menu">';
    for (var j = 0; j < pg.children.length; j++) {
      var ch = pg.children[j];
      var active = (p === ch.href) ? ' class="active"' : '';
      html += '<li><a href="' + ch.href + '"' + active + '>' + ch.label +
              '</a></li>';
    }
    html += '</ul></li>';
  } else {
    var active = (p === pg.href || (p === '/' && pg.href === '/index.html'))
                     ? ' class="active"'
                     : '';
    html +=
        '<li><a href="' + pg.href + '"' + active + '>' + pg.label + '</a></li>';
  }
}
html += '</ul>';
nav.innerHTML = html;
})();
