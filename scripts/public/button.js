
let overlayEnabled = true;

document.getElementById('postButton').addEventListener('click', function () {
  console.log('Button clicked! Current state:', overlayEnabled);
  
  // Toggle the overlay state
  overlayEnabled = !overlayEnabled;
  
  console.log('Toggling to:', overlayEnabled);
  
  // Update button text
  this.textContent = overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay';
  
  const payload = { enabled: overlayEnabled };
  console.log('Sending payload:', payload);
  
  fetch('/ctrl', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(payload)
  })
  .then(response => {
    console.log('Response status:', response.status);
    return response.json();
  })
  .then(data => {
    console.log('Server response:', data);
    if (data.status === 'ok') {
      console.log(`Performance overlay ${overlayEnabled ? 'enabled' : 'disabled'}`);
    } else {
      console.error('Failed to toggle overlay:', data.message);
    }
  })
  .catch(error => {
    console.error('Fetch error:', error);
  });
});

