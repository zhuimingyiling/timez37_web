export async function onRequestPost(context) {
    try {
        const { tableData, textData, prompt } = await context.request.json();
        
        if (!context.env.AI) {
            return new Response(JSON.stringify({ response: "请在 Cloudflare 后台绑定 AI 资源。" }), { status: 500 });
        }

        // 提取数据：如果是表格，只取前 30 行，并把每一行转成好理解的文字
        let contextContent = "";
        if (tableData && tableData.length > 0) {
            contextContent = tableData.slice(0, 30).map(row => JSON.stringify(row)).join("\n");
        } else {
            contextContent = textData || "无有效数据";
        }

        const result = await context.env.AI.run('@cf/meta/llama-3-8b-instruct', {
            messages: [
                { 
                    role: 'system', 
                    content: '你是一个贴心的中文数据助手。请直接回答用户的问题，不要说废话。如果数据是名单，请礼貌地分析或总结。' 
                },
                { 
                    role: 'user', 
                    content: `以下是数据内容：\n${contextContent}\n\n我的要求是：${prompt}` 
                }
            ]
        });

        // 核心修复：确保拿到的 response 是纯文字
        let finalResponse = "";
        if (typeof result.response === 'string') {
            finalResponse = result.response;
        } else if (result.result && result.result.response) {
            finalResponse = result.result.response;
        } else {
            finalResponse = JSON.stringify(result).replace(/["{}]/g, ''); // 兜底处理：去掉乱七八糟的符号
        }

        return new Response(JSON.stringify({ response: finalAnswerFormatter(finalResponse) }));

    } catch (err) {
        return new Response(JSON.stringify({ response: "分析遇到点小问题: " + err.message }));
    }
}

// 辅助函数：把 AI 返回的奇怪符号过滤掉
function finalAnswerFormatter(text) {
    return text.trim();
}
