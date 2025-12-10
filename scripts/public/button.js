
document.getElementById('postButton').addEventListener('click', function () {
  fetch('/stop', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({ message: 'Hello from the button!' })
  })
  .then(response => response.text())
  .then(data => console.log('Server response:', data))
  .catch(error => console.error('Error:', error));
});
