'use client';
import { useEffect, useState } from 'react';

export type Lang = 'tr' | 'en';

export function useLang(): [Lang, (l: Lang) => void] {
  const [lang, setLang] = useState<Lang>('tr');

  useEffect(() => {
    const saved = localStorage.getItem('lang');
    if (saved === 'tr' || saved === 'en') {
      setLang(saved);
    } else if (!navigator.language.toLowerCase().startsWith('tr')) {
      setLang('en');
    }
  }, []);

  const set = (l: Lang) => {
    setLang(l);
    localStorage.setItem('lang', l);
  };

  return [lang, set];
}

export function LangToggle({
  lang,
  onChange,
}: {
  lang: Lang;
  onChange: (l: Lang) => void;
}) {
  return (
    <div className="lang-toggle">
      <button
        className={lang === 'tr' ? 'active' : ''}
        onClick={() => onChange('tr')}
      >
        TR
      </button>
      <button
        className={lang === 'en' ? 'active' : ''}
        onClick={() => onChange('en')}
      >
        EN
      </button>
    </div>
  );
}
