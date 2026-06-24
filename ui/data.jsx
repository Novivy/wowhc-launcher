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
  10: '#FF7D0A', // Druid (legacy key, kept)
  11: '#FF7D0A', // Druid (real WoW class id)
};

// Group Finder: dungeon/activity id -> { name, type, art }. The art slug maps to
// groupfinder/background/ui-lfg-background-<art>.png (mirrors the WOW_HC addon's
// GF.dungeons + DUNGEON_BG tables). null art = no background image for that row.
// type: dungeon | raid | pvp | questing.
const GF_DUNGEONS = {
  389:  { name: "Ragefire Chasm",     type: 'dungeon',  art: 'ragefirechasm' },
  36:   { name: "The Deadmines",      type: 'dungeon',  art: 'deadmines' },
  43:   { name: "Wailing Caverns",    type: 'dungeon',  art: 'wailingcaverns' },
  33:   { name: "Shadowfang Keep",    type: 'dungeon',  art: 'shadowfangkeep' },
  34:   { name: "The Stockade",       type: 'dungeon',  art: 'stormwindstockades' },
  48:   { name: "Blackfathom Deeps",  type: 'dungeon',  art: 'blackfathomdeeps' },
  90:   { name: "Gnomeregan",         type: 'dungeon',  art: 'gnomeregan' },
  47:   { name: "Razorfen Kraul",     type: 'dungeon',  art: 'razorfenkraul' },
  189:  { name: "Scarlet Monastery",  type: 'dungeon',  art: 'scarletmonastery' },
  129:  { name: "Razorfen Downs",     type: 'dungeon',  art: 'razorfendowns' },
  70:   { name: "Uldaman",            type: 'dungeon',  art: 'uldaman' },
  209:  { name: "Zul'Farrak",         type: 'dungeon',  art: 'zulfarak' },
  349:  { name: "Maraudon",           type: 'dungeon',  art: 'maraudon' },
  109:  { name: "Sunken Temple",      type: 'dungeon',  art: 'sunkentemple' },
  230:  { name: "Blackrock Depths",   type: 'dungeon',  art: 'blackrockdepths' },
  229:  { name: "Blackrock Spire",    type: 'dungeon',  art: 'blackrockspire' },
  289:  { name: "Scholomance",        type: 'dungeon',  art: 'scholomance' },
  329:  { name: "Stratholme",         type: 'dungeon',  art: 'stratholme' },
  429:  { name: "Dire Maul",          type: 'dungeon',  art: 'diremaul' },
  249:  { name: "Onyxia's Lair",      type: 'raid',     art: 'onyxiaslair' },
  409:  { name: "Molten Core",        type: 'raid',     art: 'moltencore' },
  469:  { name: "Blackwing Lair",     type: 'raid',     art: 'blackwinglair' },
  309:  { name: "Zul'Gurub",          type: 'raid',     art: 'zulgurub' },
  509:  { name: "Ruins of Ahn'Qiraj", type: 'raid',     art: 'ruinsofahnqiraj' },
  531:  { name: "Ahn'Qiraj Temple",   type: 'raid',     art: 'ahnqirajtemple' },
  533:  { name: "Naxxramas",          type: 'raid',     art: 'naxxramas' },
  9003: { name: "Warsong Gulch",      type: 'pvp',      art: 'warsonggulch' },
  9002: { name: "Arathi Basin",       type: 'pvp',      art: 'arathibasin' },
  9001: { name: "Alterac Valley",     type: 'pvp',      art: 'alteracvalley' },
  9004: { name: "Mak'gora",           type: 'pvp',      art: 'makgora' },
  9005: { name: "Duels",              type: 'pvp',      art: null },
  9006: { name: "Leveling",           type: 'questing', art: 'questing' },
  9007: { name: "Duo-Leveling",       type: 'questing', art: 'questingduo' },
  9008: { name: "Looking for friends", type: 'questing', art: null },
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
  "Local Dev (192.168.0.30)",
];



window.SUB_CATEGORIES = SUB_CATEGORIES;
window.stripHtml = stripHtml;
window.NEWS = NEWS;
window.CLASS_COLORS = CLASS_COLORS;
window.GF_DUNGEONS = GF_DUNGEONS;
window.timeAgo = timeAgo;
window.REALMS = REALMS;

