// 사이드바 강의 검색 필터 + 링크 추가 시 진행중 표시
(function () {
  const input = document.getElementById('search');
  const list = document.getElementById('series-list');
  if (input && list) {
    input.addEventListener('input', function () {
      const q = input.value.trim().toLowerCase();
      list.querySelectorAll('.series').forEach(function (series) {
        let any = false;
        series.querySelectorAll('li').forEach(function (li) {
          const t = li.querySelector('.lecture-title').textContent.toLowerCase();
          const match = !q || t.includes(q);
          li.style.display = match ? '' : 'none';
          if (match) any = true;
        });
        series.style.display = any ? '' : 'none';
        if (q) series.open = true;
      });
    });
  }

  // "노트 만들기" 제출 시 버튼에 진행 표시 (서버가 동기로 자막을 받는 동안)
  document.querySelectorAll('form.hero-add').forEach(function (f) {
    f.addEventListener('submit', function () {
      const btn = f.querySelector('button');
      if (btn) {
        btn.disabled = true;
        btn.dataset.orig = btn.textContent;
        btn.textContent = '자막 받는 중…';
      }
    });
  });

  // 코딩 문제: CodeMirror 테마 에디터 (없으면 textarea 폴백)
  const editor = document.getElementById('code-editor');
  let cm = null;
  if (editor) {
    if (window.CodeMirror) {
      cm = window.CodeMirror.fromTextArea(editor, {
        mode: window.__LANG__ === 'cpp' ? 'text/x-c++src' : 'text/x-csrc',
        theme: 'material-darker',
        lineNumbers: true,
        indentUnit: 4,
        tabSize: 4,
        indentWithTabs: false,
        matchBrackets: true,
        autoCloseBrackets: true,
        extraKeys: { Tab: function (c) { c.replaceSelection('    '); } },
      });
    } else {
      // 폴백: textarea Tab 들여쓰기
      editor.addEventListener('keydown', function (e) {
        if (e.key === 'Tab') {
          e.preventDefault();
          const s = editor.selectionStart, en = editor.selectionEnd;
          editor.value = editor.value.slice(0, s) + '    ' + editor.value.slice(en);
          editor.selectionStart = editor.selectionEnd = s + 4;
        }
      });
    }
  }
  function getCode() { return cm ? cm.getValue() : (editor ? editor.value : ''); }

  // 솔루션 뷰어 (읽기 전용 CodeMirror, 같은 테마)
  let solCm = null;
  const solEl = document.getElementById('solution-code');
  if (solEl && window.CodeMirror) {
    solCm = window.CodeMirror.fromTextArea(solEl, {
      mode: window.__LANG__ === 'cpp' ? 'text/x-c++src' : 'text/x-csrc',
      theme: 'material-darker',
      lineNumbers: true,
      readOnly: 'nocursor',
    });
  }

  // 솔루션을 에디터(내 코드)로 불러오기
  const useBtn = document.getElementById('use-solution-btn');
  if (useBtn) {
    useBtn.addEventListener('click', function () {
      const solText = solCm ? solCm.getValue()
        : (solEl ? solEl.value : '');
      if (cm) cm.setValue(solText);
      else if (editor) editor.value = solText;
      const codeTab = document.querySelector('.etab[data-pane="code"]');
      if (codeTab) codeTab.click();  // 내 코드 탭으로 전환
    });
  }

  // 내 코드 / 솔루션 탭 전환
  const etabs = document.querySelectorAll('.etab');
  if (etabs.length) {
    etabs.forEach(function (tab) {
      tab.addEventListener('click', function () {
        const pane = tab.dataset.pane;
        etabs.forEach(function (t) { t.classList.toggle('active', t === tab); });
        const codeP = document.getElementById('pane-code');
        const solP = document.getElementById('pane-solution');
        if (codeP) codeP.style.display = pane === 'code' ? '' : 'none';
        if (solP) solP.style.display = pane === 'solution' ? '' : 'none';
        if (pane === 'code' && cm) cm.refresh();  // 숨김 후 표시 시 렌더 보정
        if (pane === 'solution' && solCm) solCm.refresh();
      });
    });
  }

  const runBtn = document.getElementById('run-btn');
  if (runBtn && editor) {
    const head = document.getElementById('console-head');
    const out = document.getElementById('console-out');
    const consoleBox = document.getElementById('console');
    function setHead(text, cls) {
      head.textContent = text;
      consoleBox.className = 'console' + (cls ? ' ' + cls : '');
    }
    runBtn.addEventListener('click', function () {
      runBtn.disabled = true;
      const orig = runBtn.textContent;
      runBtn.textContent = '컴파일 중…';
      setHead('컴파일 & 실행 중…', '');
      out.textContent = '';
      fetch('/problems/' + window.__PID__ + '/run', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ code: getCode() }),
      })
        .then(function (r) { return r.json(); })
        .then(function (d) {
          if (!d.ok) { setHead('오류: ' + (d.error || ''), 'bad'); return; }
          if (!d.compiled) {
            setHead('❌ 컴파일 에러', 'bad');
            out.textContent = d.compile_stderr || '';
            return;
          }
          if (d.timed_out) {
            setHead('⏱ 실행 시간 초과 (무한 루프 의심)', 'bad');
            out.textContent = d.runtime_stderr || '';
            return;
          }
          let txt = d.stdout || '';
          if (d.runtime_stderr) txt += '\n[stderr]\n' + d.runtime_stderr;
          if (d.mode === 'manual') {
            // 자동 채점 불가 — 출력 + 기대 출력 비교
            setHead('▶ 실행 완료 (출력을 기대값과 비교하세요)', '');
            if (d.expected_output) {
              txt += '\n\n──────── 기대 출력 ────────\n' + d.expected_output;
            }
            out.textContent = txt;
            return;
          }
          const pass = d.passed, total = d.total;
          const allPass = total > 0 && pass === total;
          setHead((allPass ? '✅ ' : '❌ ') + '테스트 ' + pass + ' / ' + total + ' 통과',
                  allPass ? 'good' : 'bad');
          out.textContent = txt;
          // 사이드바 점 갱신 (현재 문제)
          const link = document.querySelector('.lecture-link.active .status-dot');
          if (link) link.className = 'status-dot ' + (allPass ? 'status-note' : 'status-tx');
        })
        .catch(function () { setHead('실행 실패', 'bad'); })
        .finally(function () { runBtn.disabled = false; runBtn.textContent = orig; });
    });
  }

  // 자막 IP 차단 상태 확인 (대시보드에서 바로)
  const ipBtn = document.getElementById('ip-check-btn');
  const ipOut = document.getElementById('ip-status-result');
  if (ipBtn && ipOut) {
    ipBtn.addEventListener('click', function () {
      ipOut.textContent = '확인 중…';
      ipOut.className = 'ip-result';
      fetch('/api/ip-status')
        .then(function (r) { return r.json(); })
        .then(function (d) {
          if (!d.ok) { ipOut.textContent = '오류: ' + (d.error || ''); ipOut.className = 'ip-result bad'; return; }
          if (d.blocked) {
            ipOut.textContent = '❌ 차단됨 · 자막 누락 ' + d.missing + '개';
            ipOut.className = 'ip-result bad';
          } else {
            ipOut.textContent = '✅ 정상 · 자막 누락 ' + d.missing + '개';
            ipOut.className = 'ip-result good';
          }
        })
        .catch(function () { ipOut.textContent = '확인 실패'; ipOut.className = 'ip-result bad'; });
    });
  }
})();
