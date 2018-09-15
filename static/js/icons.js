---
---
document.addEventListener("DOMContentLoaded", function() {
    var docs = {
    {% for doc in site.data.docsets %}
        '{{ doc.title }}': '{{ doc.icon }}'{% unless forloop.last %},{% endunless %}
    {% endfor %}
    };

    [].forEach.call(document.getElementById('docset-list').querySelectorAll('li'), function(el) {
        el.setAttribute('style', 'background-image: url(data:image/png;base64,' + docs[el.textContent] + ')');
    });
});
document.addEventListener("DOMContentLoaded", function() {
    var docs = {
    {% for doc in site.data.docsets_usercontributed %}
        '{{ doc.title }}': '{{ doc.icon }}'{% unless forloop.last %},{% endunless %}
    {% endfor %}
    };

    [].forEach.call(document.getElementById('docset-list-contrib').querySelectorAll('li'), function(el) {
        el.setAttribute('style', 'background-image: url(data:image/png;base64,' + docs[el.textContent] + ')');
    });
});
