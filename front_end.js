

const API_BASE = 'http://localhost:18080';
 
document.addEventListener('DOMContentLoaded', () => {
    const roleSelect = document.getElementById('role');
    const workerIdGroup = document.getElementById('workerIdGroup');
    const idLabel = document.getElementById('idLabel');
    const workerIdInput = document.getElementById('workerId');
    const loginForm = document.getElementById('loginForm');
    const signupForm = document.getElementById('signupForm');
 
    if (roleSelect && workerIdGroup) {
        const toggleIdField = () => {
            const role = roleSelect.value;
            if (role === 'Employee' || role === 'Admin') {
                workerIdGroup.style.display = 'block';
                if (workerIdInput) workerIdInput.setAttribute('required', 'true');
                if (idLabel) {
                    idLabel.textContent = role === 'Admin' ? 'Admin ID' : 'Worker ID / Badge Number';
                }
            } else {
                workerIdGroup.style.display = 'none';
                if (workerIdInput) {
                    workerIdInput.removeAttribute('required');
                    workerIdInput.value = '';
                }
            }
        };
 
        roleSelect.addEventListener('change', toggleIdField);
        toggleIdField();
    }
 
    // Asks the browser for the current location. Resolves to null if the
    // user denies permission, the browser doesn't support it, or it times
    // out — login should never be blocked just because location failed.
    function getLocation() {
        return new Promise((resolve) => {
            if (!navigator.geolocation) {
                resolve(null);
                return;
            }
            navigator.geolocation.getCurrentPosition(
                (position) => {
                    resolve({
                        latitude: position.coords.latitude,
                        longitude: position.coords.longitude
                    });
                },
                () => resolve(null),
                { timeout: 5000 }
            );
        });
    }
 
    function redirectForRole(role) {
        if (role === 'Admin') {
            window.location.href = 'admin.html';
        } else if (role === 'Employee') {
            window.location.href = 'employee.html';
        } else {
            window.location.href = 'user.html';
        }
    }
 
    if (loginForm) {
        loginForm.addEventListener('submit', async (e) => {
            e.preventDefault();
 
            const submitBtn = loginForm.querySelector('.submit-btn');
            const usernameInput = document.getElementById('username');
            const passwordInput = document.getElementById('password');
 
            const username = usernameInput ? usernameInput.value : '';
            const password = passwordInput ? passwordInput.value : '';
 
            if (submitBtn) submitBtn.disabled = true;
 
            const location = await getLocation();
 
            try {
                const response = await fetch(`${API_BASE}/api/login`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
                    body: JSON.stringify({
                        username,
                        password,
                        latitude: location ? location.latitude : null,
                        longitude: location ? location.longitude : null
                    })
                });
 
                const data = await response.json();
 
                if (data.locked) {
                    alert(data.message || 'This account has been locked after too many failed attempts.');
                } else if (data.success) {
                    redirectForRole(data.role);
                } else {
                    alert(data.message || 'Incorrect username or password.');
                }
            } catch (err) {
                alert('Could not reach the server. Please make sure the backend is running.');
            } finally {
                if (submitBtn) submitBtn.disabled = false;
            }
        });
    }
 
    if (signupForm) {
        signupForm.addEventListener('submit', async (e) => {
            e.preventDefault();
 
            const submitBtn = signupForm.querySelector('.submit-btn');
            const role = roleSelect ? roleSelect.value : 'User';
            const workerId = workerIdInput ? workerIdInput.value : '';
            const username = document.getElementById('username').value;
            const email = document.getElementById('email').value;
            const password = document.getElementById('password').value;
 
            if (submitBtn) submitBtn.disabled = true;
 
            try {
                const response = await fetch(`${API_BASE}/api/signup`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
                    body: JSON.stringify({ role, workerId, username, email, password })
                });
 
                const data = await response.json();
 
                if (data.success) {
                    alert('Account successfully created! Please log in.');
                    window.location.href = 'login.html';
                } else {
                    alert(data.message || 'Could not create account. Please try again.');
                }
            } catch (err) {
                alert('Could not reach the server. Please make sure the backend is running.');
            } finally {
                if (submitBtn) submitBtn.disabled = false;
            }
        });
    }
});
 
function logout() {
    window.location.href = 'login.html';
}
 
