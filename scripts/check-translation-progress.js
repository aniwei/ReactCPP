#!/usr/bin/env node

/**
 * Translation Progress Checker
 * 
 * This script analyzes the react-source-mapping.csv file and provides
 * a summary of translation progress across different modules.
 */

const fs = require('fs');
const path = require('path');

const CSV_PATH = path.join(__dirname, '../docs/matrix/react-source-mapping.csv');
const TODO_PATH = path.join(__dirname, '../docs/matrix/react-translation-todo.md');

function parseCSV(content) {
  const lines = content.trim().split('\n');
  const headers = lines[0].split(',');
  
  return lines.slice(1).map(line => {
    const values = line.split(',');
    const row = {};
    headers.forEach((header, index) => {
      row[header] = values[index] || '';
    });
    return row;
  });
}

function analyzeProgress(data) {
  const byModule = {};
  const statusCount = {
    'complete': 0,
    'in-progress': 0,
    'not-started': 0
  };

  data.forEach(row => {
    const module = row.module_category;
    const status = row.status;
    
    if (!byModule[module]) {
      byModule[module] = {
        complete: 0,
        'in-progress': 0,
        'not-started': 0,
        total: 0
      };
    }
    
    byModule[module][status]++;
    byModule[module].total++;
    statusCount[status]++;
  });

  return { byModule, statusCount, total: data.length };
}

function printProgress(analysis) {
  console.log('🚀 ReactCPP Translation Progress Report\n');
  console.log('=' * 50);
  
  // Overall progress
  const { statusCount, total } = analysis;
  const completePercent = Math.round((statusCount.complete / total) * 100);
  const inProgressPercent = Math.round((statusCount['in-progress'] / total) * 100);
  
  console.log('\n📊 Overall Progress:');
  console.log(`✅ Complete: ${statusCount.complete}/${total} (${completePercent}%)`);
  console.log(`🔄 In Progress: ${statusCount['in-progress']}/${total} (${inProgressPercent}%)`);
  console.log(`⛔ Not Started: ${statusCount['not-started']}/${total} (${Math.round((statusCount['not-started'] / total) * 100)}%)`);
  
  // Progress bar
  const progressBar = '█'.repeat(Math.floor(completePercent / 5)) + 
                     '▓'.repeat(Math.floor(inProgressPercent / 5)) +
                     '░'.repeat(Math.floor((statusCount['not-started'] / total * 100) / 5));
  console.log(`\n[${progressBar}] ${completePercent + inProgressPercent}% overall`);
  
  // By module
  console.log('\n📋 Progress by Module:');
  Object.entries(analysis.byModule).forEach(([module, stats]) => {
    const modulePercent = Math.round((stats.complete / stats.total) * 100);
    console.log(`\n${module.toUpperCase()}:`);
    console.log(`  ✅ ${stats.complete}/${stats.total} complete (${modulePercent}%)`);
    console.log(`  🔄 ${stats['in-progress']} in progress`);
    console.log(`  ⛔ ${stats['not-started']} not started`);
  });
}

function suggestNextSteps(analysis) {
  console.log('\n🎯 Suggested Next Steps:\n');
  
  // Find modules with in-progress items
  const inProgressModules = Object.entries(analysis.byModule)
    .filter(([_, stats]) => stats['in-progress'] > 0)
    .sort((a, b) => b[1]['in-progress'] - a[1]['in-progress']);
  
  if (inProgressModules.length > 0) {
    console.log('1. Complete in-progress modules:');
    inProgressModules.forEach(([module, stats]) => {
      console.log(`   - ${module}: ${stats['in-progress']} files to finish`);
    });
  }
  
  // Find modules not started
  const notStartedModules = Object.entries(analysis.byModule)
    .filter(([_, stats]) => stats['not-started'] > 0 && stats['complete'] === 0)
    .sort((a, b) => a[1]['not-started'] - b[1]['not-started']);
  
  if (notStartedModules.length > 0) {
    console.log('\n2. Consider starting these modules:');
    notStartedModules.slice(0, 3).forEach(([module, stats]) => {
      console.log(`   - ${module}: ${stats['not-started']} files`);
    });
  }
}

// Main execution
try {
  if (!fs.existsSync(CSV_PATH)) {
    console.error(`❌ CSV file not found: ${CSV_PATH}`);
    process.exit(1);
  }
  
  const csvContent = fs.readFileSync(CSV_PATH, 'utf8');
  const data = parseCSV(csvContent);
  const analysis = analyzeProgress(data);
  
  printProgress(analysis);
  suggestNextSteps(analysis);
  
  console.log('\n🔗 For detailed TODO items, see:');
  console.log(`   ${path.relative(process.cwd(), TODO_PATH)}`);
  
} catch (error) {
  console.error('❌ Error analyzing translation progress:', error.message);
  process.exit(1);
}