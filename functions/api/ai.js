export async function onRequestPost(context) {
    try {
        // 1. 获取前端传来的数据
        const requestData = await context.request.json();
        const { tableData, textData, prompt } = requestData;
        
        // 2. 检查 Cloudflare 后台是否绑定了 AI 资源
        if (!context.env.AI) {
            return new Response(JSON.stringify({ response: "错误：未在 Cloudflare 后台绑定 AI 资源！请检查设置 -> 函数 -> AI 绑定。" }), { status: 500 });
        }

        // 3. 整理发送给 AI 的内容（优先处理文字，没有文字则处理表格）
        let dataContext = "";
        if (textData) {
            dataContext = textData;
        } else if (tableData) {
            dataContext = JSON.stringify(tableData.slice(0, 50)); // 只取前50行防止超出AI处理长度
        } else {
            dataContext = "暂无数据内容";
        }

        // 4. 调用 Cloudflare 内置模型
        const result = await context.env.AI.run('@cf/meta/llama-3-8b-instruct', {
            messages: [
                { role: 'system', content: '你是一个专业的数据助手，请基于提供的内容回答用户问题。' },
                { role: 'user', content: `已知数据内容：\n${dataContext}\n\n我的问题是：${prompt}` }
            ]
        });

        // 5. 返回结果给网页
        const finalAnswer = result.response || result.result || "AI 忙碌中，请稍后再试。";
        return new Response(JSON.stringify({ response: finalAnswer }));

    } catch (err) {
        // 捕捉错误并返回，防止页面长时间没反应
        return new Response(JSON.stringify({ response: "后端运行出错: " + err.message }), { status: 500 });
    }
}
