/**
888888          888888     888     8888888888888Y88b   d88P 
888888          888888     888     888888        Y88b d88P  
888888          888888     888     888888         Y88o88P   
888888          888888     Y88b   d88P8888888      Y888P    
888888          888888      Y88b d88P 888          d888b    
888888          888888       Y88o88P  888         d88888b   
YY8888bb      dd8888PP        Y888P   888        d88P Y88b  
YY8888bb      dd8888PP         Y8P    8888888888d88P   Y88b 
  YY8888bb  dd8888PP       8888888888888888888888888888b.   
  YY8888bb  dd8888PP           888    888       888  "Y88b  
    YY8888oo8888PP             888    888       888    888  
    YY8888oo8888PP             888    8888888   888    888  
      YY888888PP               888    888       888    888  
      YY888888PP               888    888       888    888  
        YY88PP                 888    888       888  .d88P  
        YY88PP                 888    88888888888888888P"

 * main.js
 * -----------------------------------------------------------------------
 * Entry point. The page works fully with this file absent everything
 * here is progressive enhancement, never a requirement for content to
 * appear. Keep it that way: no critical content should ever be injected
 * only through JS on this homepage.
 *
 * This file is intentionally small for now. As the project grows, split
 * new behavior into its own module under js/ and import it here rather
 * than growing this file, e.g.:
 *
 *   js/
 *   ├── main.js            <- entry point (this file)
 *   ├── version.js          <- e.g. fetch latest release tag from GitHub
 *   ├── theme.js            <- e.g. light/dark or high-contrast toggle
 *   └── shortcuts.js        <- e.g. "press / to search docs" style UX
 *
 * import { initVersionBadge } from './version.js';
 * -----------------------------------------------------------------------
 */

function setFooterYear() {
  const el = document.querySelector('[data-year]');
  if (el) {
    el.textContent = String(new Date().getFullYear());
  }
}

function init() {
  setFooterYear();
}

document.addEventListener('DOMContentLoaded', init);
