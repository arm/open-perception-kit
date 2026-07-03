(() => {
  const rootId = 'pek-report-tools';
  const selectors = [
    ['.chip-header', (element) => element.textContent],
    ['.test-file-title', (element) => element.textContent],
    ['.test-error-container', () => 'Errors'],
    ['.metadata-view', () => 'Metadata'],
    ['video', () => 'Video'],
    ['img.screenshot', () => 'Screenshot'],
    ['[id^="attachment-"]', (element) => element.id.replace(/^attachment-/, 'Attachment: ')]
  ];

  const clean = (text) => (text || '').replace(/\s+/g, ' ').trim();
  const sourceExtensions = new Set([
    'c', 'cc', 'cpp', 'cxx', 'h', 'hh', 'hpp', 'js', 'jsx', 'mjs', 'cjs',
    'ts', 'tsx', 'py', 'sh', 'bash', 'cmake', 'txt', 'json', 'yaml', 'yml', 'md'
  ]);
  let sourceConfig;

  const getSourceConfig = () => {
    if (sourceConfig) {
      return sourceConfig;
    }

    const bar = document.querySelector('.pek-report-bar');
    const repository = bar?.dataset.repository;
    const commit = bar?.dataset.commit;
    if (!repository || !commit) {
      return undefined;
    }

    try {
      sourceConfig = {
        repository,
        commit,
        files: JSON.parse(document.getElementById('pek-report-source-map')?.textContent || '{}')
      };
    } catch {
      sourceConfig = { repository, commit, files: {} };
    }
    return sourceConfig;
  };

  const sourcePathFor = (file) => {
    const source = getSourceConfig();
    if (!source) {
      return '';
    }
    const path = file.replace(/^\.\//, '');
    return source.files[path] || '';
  };

  const sourceHref = (file, line) => {
    const source = getSourceConfig();
    const path = sourcePathFor(file);
    if (!source || !path) {
      return '';
    }
    const encodedPath = path.split('/').map(encodeURIComponent).join('/');
    const lineSuffix = line ? `#L${line}` : '';
    return `https://github.com/${source.repository}/blob/${source.commit}/${encodedPath}${lineSuffix}`;
  };

  const isSourceTokenChar = (char) => {
    const code = char.charCodeAt(0);
    return (code >= 48 && code <= 57) ||
      (code >= 65 && code <= 90) ||
      (code >= 97 && code <= 122) ||
      char === '_' ||
      char === '-' ||
      char === '.' ||
      char === '/' ||
      char === ':';
  };

  const trimSourceToken = (token) => {
    let start = 0;
    let end = token.length;
    while (start < end && !isSourceTokenChar(token[start])) {
      start += 1;
    }
    while (end > start && !isSourceTokenChar(token[end - 1])) {
      end -= 1;
    }
    return token.slice(start, end);
  };

  const sourceReferenceFromToken = (token) => {
    const reference = trimSourceToken(token);
    const [file, line] = reference.split(':');
    const extension = file?.slice(file.lastIndexOf('.') + 1).toLowerCase();
    if (!extension || !sourceExtensions.has(extension)) {
      return undefined;
    }
    const href = sourceHref(file, /^\d+$/.test(line || '') ? line : '');
    return href ? { href, text: reference } : undefined;
  };

  const sourceReferenceFrom = (text) => {
    for (const token of clean(text).split(/\s+/)) {
      const reference = sourceReferenceFromToken(token);
      if (reference) {
        return reference;
      }
    }
    return undefined;
  };

  const ensureId = (element, index) => {
    if (element.id) {
      return element.id;
    }
    if (!element.dataset.pekSectionId) {
      element.dataset.pekSectionId = `pek-report-section-${index}`;
    }
    element.id = element.dataset.pekSectionId;
    return element.id;
  };

  const collectSections = () => {
    const seen = new Set();
    const sections = [];

    for (const [selector, labelFor] of selectors) {
      for (const element of document.querySelectorAll(selector)) {
        if (seen.has(element) || element.closest(`#${rootId}`)) {
          continue;
        }
        const label = clean(labelFor(element));
        if (!label) {
          continue;
        }
        seen.add(element);
        sections.push({ element, id: ensureId(element, sections.length), label });
        if (sections.length >= 40) {
          return sections;
        }
      }
    }

    return sections;
  };

  const replaceWithSourceLink = (element, reference, prefix = '') => {
    if (element.dataset.pekSourceLinked) {
      return;
    }
    const link = document.createElement('a');
    link.className = 'pek-source-link';
    link.href = reference.href;
    link.textContent = reference.text;
    link.addEventListener('click', (event) => event.stopPropagation());
    element.replaceChildren(document.createTextNode(prefix), link);
    element.dataset.pekSourceLinked = 'true';
  };

  const linkSourceReferences = () => {
    if (!getSourceConfig()) {
      return;
    }

    for (const element of document.querySelectorAll('.chip-header-allow-selection')) {
      const reference = sourceReferenceFrom(element.textContent);
      if (reference) {
        replaceWithSourceLink(element, reference);
      }
    }

    for (const link of document.querySelectorAll('.test-file-path-link')) {
      const reference = sourceReferenceFrom(link.textContent);
      if (reference && !link.dataset.pekSourceLinked) {
        link.href = reference.href;
        link.classList.add('pek-source-link');
        link.addEventListener('click', (event) => event.stopPropagation());
        link.dataset.pekSourceLinked = 'true';
      }
    }

    for (const element of document.querySelectorAll('.test-case-location, .test-result-path')) {
      const reference = sourceReferenceFrom(element.textContent);
      if (reference) {
        replaceWithSourceLink(element, reference, element.classList.contains('test-result-path') ? '\u2014 ' : '');
      }
    }
  };

  const updateBackLink = () => {
    const link = document.querySelector('.pek-report-back');
    if (!link) {
      return;
    }
    if (!link.dataset.indexHref) {
      link.dataset.indexHref = link.getAttribute('href') || '';
      link.dataset.indexText = link.textContent || 'Back to report index';
    }

    const params = new URLSearchParams(location.hash.startsWith('#?') ? location.hash.slice(2) : '');
    if (!params.has('testId')) {
      link.href = link.dataset.indexHref;
      link.textContent = link.dataset.indexText;
      return;
    }

    params.delete('testId');
    params.delete('run');
    params.delete('anchor');
    const hash = params.toString();
    const hashSuffix = hash ? `#?${hash}` : '';
    link.href = `${location.pathname}${location.search}${hashSuffix}`;
    link.textContent = 'Back to tests';
  };

  const init = () => {
    if (document.getElementById(rootId)) {
      return;
    }

    const root = document.createElement('div');
    const jump = document.createElement('select');
    const top = document.createElement('button');
    let rebuildTimer = 0;

    root.id = rootId;
    root.className = 'pek-report-float';
    jump.className = 'pek-report-jump';
    jump.setAttribute('aria-label', 'Jump to report section');
    top.className = 'pek-report-top';
    top.type = 'button';
    top.textContent = 'Top';
    top.hidden = true;
    root.append(jump, top);
    document.body.append(root);

    const syncRoot = () => {
      root.hidden = jump.hidden && top.hidden;
    };

    const rebuild = () => {
      const sections = collectSections();
      jump.replaceChildren(new Option('Jump', ''));
      for (const section of sections) {
        jump.add(new Option(section.label, section.id));
      }
      jump.hidden = sections.length === 0;
      syncRoot();
    };

    const refresh = () => {
      rebuild();
      linkSourceReferences();
      updateBackLink();
    };

    const scheduleRefresh = () => {
      clearTimeout(rebuildTimer);
      rebuildTimer = setTimeout(refresh, 250);
    };

    jump.addEventListener('change', () => {
      const target = document.getElementById(jump.value);
      jump.value = '';
      target?.scrollIntoView({ behavior: 'smooth', block: 'start' });
    });
    top.addEventListener('click', () => window.scrollTo({ top: 0, behavior: 'smooth' }));
    window.addEventListener('scroll', () => {
      top.hidden = window.scrollY < 240;
      syncRoot();
    }, { passive: true });
    window.addEventListener('hashchange', updateBackLink);
    window.addEventListener('popstate', updateBackLink);
    new MutationObserver((mutations) => {
      if (mutations.every((mutation) => root.contains(mutation.target))) {
        return;
      }
      scheduleRefresh();
    }).observe(document.body, { childList: true, subtree: true });

    refresh();
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
