// main.js
document.addEventListener('DOMContentLoaded', () => {
    const computeButton = document.getElementById('computeButton');
    const resultDiv = document.getElementById('output');
    const inputArea = document.getElementById('input');

    // Create a Web Worker
    const worker = new Worker('worker.js');

    // Listen for messages from the worker
    worker.onmessage = function(e) {
        resultDiv.textContent = `Result: ${e.data}`;
    };

    // Error handling for worker
    worker.onerror = function(error) {
        resultDiv.textContent = `Worker Error: ${error.message}`;
        console.error('Worker error:', error);
    };

    // Send message to worker to start computation when button is clicked
    computeButton.addEventListener('click', () => {
        resultDiv.textContent = 'Computing...';
        worker.postMessage(inputArea.value);
    });
});