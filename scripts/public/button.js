
let overlayEnabled = true;

document.addEventListener('DOMContentLoaded', function() {
  const button = document.getElementById('postButton');
  
  if (!button) {
    console.error('postButton not found in DOM');
    return;
  }
  
  button.addEventListener('click', function () {
    // Toggle the state
    overlayEnabled = !overlayEnabled;
    
    console.log('Toggling performance overlay to:', overlayEnabled);
    
    // Update button text
    const icon = '<span class="btn-icon">👁</span> ';
    this.innerHTML = icon + (overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay');
    
    fetch('/ctrl', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({ enabled: overlayEnabled })
    })
    .then(response => response.json())
    .then(data => {
      console.log('Server response:', data);
      if (data.status !== 'ok') {
        console.error('Server returned error:', data);
      }
    })
    .catch(error => {
      console.error('Error:', error);
      // Revert state on error
      overlayEnabled = !overlayEnabled;
      this.innerHTML = icon + (overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay');
    });
  });
});
