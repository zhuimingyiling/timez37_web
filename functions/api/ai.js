export async function onRequestPost(context) {
    try {
        const { tableData, textData, prompt } = await context.request.json();
        
        if (!context.env.AI) {
            return new Response(JSON.stringify({ response: "错误：未在后台绑定 AI 资源！" }), { status: 500 });
        }

        // 构造发送给 AI 的数据
        let dataContext = textData || JSON.stringify(tableData);

        const result = await context.env.AI.run('@cf/meta/llama-3-8b-instruct', {
            messages: [
                { role: 'system', content: '你是一个专业分析师。请基于提供的数据回答问题。' },
                { role: 'user', content: `数据：\n${dataContext}\n\n指令：${prompt}` }
            ]
        });

        // 兼容不同版本的 Cloudflare AI 返回格式
        const finalMsg = result.response || result.result || "AI 已响应但未产生文本。";
        return new Response(JSON.stringify({ response: finalMsg }));

    } catch (err) {
        return new Response(JSON.stringify({ response: "后端出错: " + err.message }), { status: 500 });
    }
}
