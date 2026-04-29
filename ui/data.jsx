// Shared mock data + tone copy (wow-hc.com flavored — direct, server-admin, grim)
const SUB_CATEGORIES = {
  1:  { name: 'Quest',        icon: 'scroll'         },
  2:  { name: 'Instance/Raid',icon: 'onyxia'         },
  3:  { name: 'Other',        icon: 'enchant'        },
  4:  { name: 'Warrior',      icon: 'class-1'        },
  5:  { name: 'Paladin',      icon: 'class-2'        },
  6:  { name: 'Hunter',       icon: 'class-3'        },
  7:  { name: 'Rogue',        icon: 'class-4'        },
  8:  { name: 'Priest',       icon: 'class-5'        },
  9:  { name: 'Shaman',       icon: 'class-7'        },
  10: { name: 'Mage',         icon: 'class-8'        },
  11: { name: 'Warlock',      icon: 'class-9'        },
  12: { name: 'Druid',        icon: 'class-11'       },
  13: { name: 'News',         icon: 'achievement-64' },
  14: { name: 'Horde',        icon: 'faction-h'      },
  15: { name: 'Alliance',     icon: 'faction-a'      },
  16: { name: 'Announcement', icon: 'scroll'         },
  17: { name: 'Patch Notes',  icon: 'achievement-64' },
  18: { name: 'Hotfix',       icon: 'gear'           },
  19: { name: 'Death Appeal', icon: 'res'            },
};

const NEWS = [
 ];

const CLASS_COLORS = {
  1: '#C79C6E', // Warrior
  2: '#F58CBA', // Paladin
  3: '#ABD473', // Hunter
  4: '#FFF569', // Rogue
  5: '#FFFFFF', // Priest
  7: '#0070DE', // Shaman
  8: '#69CCF0', // Mage
  9: '#9482C9', // Warlock
  10: '#FF7D0A', // Druid
};

function stripHtml(str) {
  if (!str) return '';
  return str
    .replace(/[‹<][^›>]*[›>]/g, ' ')
    .replace(/&[a-zA-Z#0-9]+;/g, ' ')
    .replace(/\s{2,}/g, ' ')
    .trim();
}

function timeAgo(ts) {
  const diff = Math.floor(Date.now() / 1000) - ts;
  if (diff < 60)    return diff + 's';
  if (diff < 3600)  return Math.floor(diff / 60) + 'm';
  if (diff < 86400) return Math.floor(diff / 3600) + 'h';
  return Math.floor(diff / 86400) + 'd';
}

const REALMS = [
  "Permadeath (Normal) / Dawnrest (PVP)",
  "Player Testing Realm (PTR)",
];



window.SUB_CATEGORIES = SUB_CATEGORIES;
window.stripHtml = stripHtml;
window.NEWS = NEWS;
window.CLASS_COLORS = CLASS_COLORS;
window.timeAgo = timeAgo;
window.REALMS = REALMS;

