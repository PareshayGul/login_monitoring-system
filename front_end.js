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

    if (loginForm) {
        loginForm.addEventListener('submit', (e) => {
            e.preventDefault();
            const role = roleSelect.value;
            if (role === 'Admin') {
                window.location.href = 'admin.html';
            } else if (role === 'Employee') {
                window.location.href = 'employee.html';
            } else {
                window.location.href = 'user.html';
            }
        });
    }

    if (signupForm) {
        signupForm.addEventListener('submit', (e) => {
            e.preventDefault();
            alert('Account successfully created! Please log in.');
            window.location.href = 'login.html';
        });
    }
});

function logout() {
    window.location.href = 'login.html';
}